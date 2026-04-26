"""Blocking MQTT v5 client helper for integration tests.

This module wraps paho-mqtt with test-oriented convenience methods.
"""

from __future__ import annotations

import os
from queue import Empty, Queue
import re
from threading import Event, Lock
import time
from typing import Any

try:
    import paho.mqtt.client as mqtt
    from paho.mqtt.packettypes import PacketTypes
    from paho.mqtt.properties import Properties
    from paho.mqtt.subscribeoptions import SubscribeOptions
    _PAHO_IMPORT_ERROR: Exception | None = None
except ImportError as import_error:
    mqtt = None  # type: ignore[assignment]
    PacketTypes = Any  # type: ignore[assignment]
    Properties = Any  # type: ignore[assignment]
    SubscribeOptions = Any  # type: ignore[assignment]
    _PAHO_IMPORT_ERROR = import_error


DEFAULT_TIMEOUT_SECONDS = 8.0
_DISCONNECT_REASON_PATTERN = re.compile(r"Received DISCONNECT\s*\(([^)]*)\)")
_PAHO_DISCONNECT_PARSER_PATCHED = False


def _patch_paho_disconnect_parser_if_needed() -> None:
    global _PAHO_DISCONNECT_PARSER_PATCHED
    if _PAHO_IMPORT_ERROR is not None or _PAHO_DISCONNECT_PARSER_PATCHED:
        return
    if not hasattr(mqtt.Client, "_handle_disconnect"):
        return

    original_handle_disconnect = mqtt.Client._handle_disconnect

    # paho versions with this behavior only parse reason codes for remaining
    # length > 2, which drops valid MQTT v5 DISCONNECT packets with
    # remaining length 2 (reason code + zero properties length).
    def _patched_handle_disconnect(self) -> None:  # type: ignore[no-untyped-def]
        packet_type = mqtt.DISCONNECT >> 4
        reason_code_value = None
        properties_value = None
        remaining_length = self._in_packet["remaining_length"]

        if remaining_length > 1:
            reason_code_value = mqtt.ReasonCode(packet_type)
            reason_code_value.unpack(self._in_packet["packet"])
            if remaining_length > 2:
                properties_value = mqtt.Properties(packet_type)
                properties_value.unpack(self._in_packet["packet"][1:])

        self._easy_log(
            mqtt.MQTT_LOG_DEBUG,
            "Received DISCONNECT %s %s",
            reason_code_value,
            properties_value,
        )
        self._sock_close()
        self._do_on_disconnect(
            packet_from_broker=True,
            v1_rc=mqtt.MQTTErrorCode.MQTT_ERR_SUCCESS,
            reason=reason_code_value,
            properties=properties_value,
        )

    mqtt.Client._handle_disconnect = _patched_handle_disconnect
    setattr(mqtt.Client, "_integration_original_handle_disconnect", original_handle_disconnect)
    _PAHO_DISCONNECT_PARSER_PATCHED = True


class ConnackResult:
    """Result returned by connect()."""

    def __init__(self, reason_code: int, session_present: bool, properties: Properties | None) -> None:
        self.reason_code = reason_code
        self.session_present = session_present
        self.properties = properties


class ReceivedMessage:
    """Inbound publish message details collected from callbacks."""

    def __init__(
        self,
        topic: str,
        payload: bytes,
        qos: int,
        retain: bool,
        dup: bool,
        mid: int,
        properties: Properties | None,
    ) -> None:
        self.topic = topic
        self.payload = payload
        self.qos = qos
        self.retain = retain
        self.dup = dup
        self.mid = mid
        self.properties = properties


class DisconnectEvent:
    """Server initiated disconnect details."""

    def __init__(
        self,
        reason_code: int,
        properties: Properties | None,
        from_broker: bool | None = None,
    ) -> None:
        self.reason_code = reason_code
        self.properties = properties
        self.from_broker = from_broker


class MqttClient:
    """High-level MQTT client with blocking methods for integration tests."""

    def __init__(
        self,
        *,
        client_id: str = "",
        protocol: int | None = None,
        transport: str = "tcp",
        timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS,
        max_inflight_messages: int | None = None,
    ) -> None:
        self._ensure_dependency_available()
        self._client_id = client_id
        self._protocol = mqtt.MQTTv5 if protocol is None else protocol
        self._transport = transport
        self._timeout_seconds = timeout_seconds
        self._max_inflight_messages = max_inflight_messages

        self._client: mqtt.Client | None = None
        self._connack_event = Event()
        self._disconnect_event = Event()
        self._connack_result: ConnackResult | None = None
        self._disconnect_result: DisconnectEvent | None = None
        self._disconnect_reason_from_log: int | None = None

        self._published_mids: dict[int, int] = {}
        self._suback_mids: dict[int, list[int]] = {}
        self._unsuback_mids: dict[int, list[int]] = {}
        self._mid_lock = Lock()

        self._inbound_messages: Queue[ReceivedMessage] = Queue()

        self._will_payload: bytes | None = None
        self._will_topic: str | None = None
        self._will_qos: int = 0
        self._will_retain: bool = False
        self._will_properties: Properties | None = None

        self._topic_alias_maximum = 0
        self._next_topic_alias = 1
        self._topic_to_alias: dict[str, int] = {}

    @staticmethod
    def _ensure_dependency_available() -> None:
        if _PAHO_IMPORT_ERROR is not None:
            raise RuntimeError(
                "paho-mqtt is required for integration test helpers. "
                "Install with: pip install paho-mqtt"
            ) from _PAHO_IMPORT_ERROR
        _patch_paho_disconnect_parser_if_needed()

    def __enter__(self) -> MqttClient:
        return self

    def __exit__(self, _exc_type: Any, _exc_val: Any, _exc_tb: Any) -> bool:
        try:
            if self._client is not None and self._client.is_connected():
                self.disconnect()
        except Exception:
            if self._client is not None:
                self._client.loop_stop()
        return False

    def set_will(
        self,
        *,
        topic: str,
        payload: bytes | str,
        qos: int = 0,
        retain: bool = False,
        delay: int | None = None,
        properties: Properties | None = None,
    ) -> None:
        """Configure the will message that is applied on the next connect()."""
        self._will_topic = topic
        if isinstance(payload, str):
            self._will_payload = payload.encode("utf-8")
        else:
            self._will_payload = payload
        self._will_qos = qos
        self._will_retain = retain

        will_properties = properties
        if will_properties is None:
            will_properties = Properties(PacketTypes.WILLMESSAGE)
        if delay is not None:
            setattr(will_properties, "WillDelayInterval", int(delay))
        self._will_properties = will_properties

    def enable_topic_alias(self, maximum: int) -> None:
        """Enable outbound topic alias auto resolution for publish()."""
        self._topic_alias_maximum = max(0, int(maximum))
        self._next_topic_alias = 1
        self._topic_to_alias.clear()

    def connect(
        self,
        host: str,
        port: int,
        client_id: str = "",
        clean_start: bool = True,
        keepalive: int = 60,
        properties: Properties | None = None,
        username: str | None = None,
        password: str | None = None,
    ) -> ConnackResult:
        """Connect to broker and wait for CONNACK."""
        self._create_client(client_id or self._client_id)
        assert self._client is not None

        self._connack_event.clear()
        self._disconnect_event.clear()
        self._connack_result = None
        self._disconnect_result = None

        if username is not None:
            self._client.username_pw_set(username=username, password=password)

        if self._will_topic is not None and self._will_payload is not None:
            self._client.will_set(
                self._will_topic,
                payload=self._will_payload,
                qos=self._will_qos,
                retain=self._will_retain,
                properties=self._will_properties,
            )

        self._client.connect(
            host,
            port,
            keepalive=keepalive,
            clean_start=clean_start,
            properties=properties,
        )
        self._client.loop_start()

        if not self._connack_event.wait(timeout=self._timeout_seconds):
            raise TimeoutError("Timed out while waiting for CONNACK")
        if self._connack_result is None:
            raise RuntimeError("Missing CONNACK result")
        return self._connack_result

    def disconnect(
        self,
        reason_code: int = 0,
        properties: Properties | None = None,
    ) -> None:
        """Disconnect and wait for a clean socket close."""
        if self._client is None:
            return

        self._disconnect_result = None
        self._disconnect_reason_from_log = None
        self._disconnect_event.clear()
        disconnect_reason = mqtt.ReasonCode(mqtt.DISCONNECT >> 4, identifier=int(reason_code))
        self._client.disconnect(reasoncode=disconnect_reason, properties=properties)

        if not self._disconnect_event.wait(timeout=self._timeout_seconds):
            raise TimeoutError("Timed out while waiting for disconnect")

        self._client.loop_stop()

    def publish(
        self,
        topic: str,
        payload: bytes | str,
        qos: int = 0,
        retain: bool = False,
        properties: Properties | None = None,
        wait_for_qos0_publish: bool = True,
        wait_for_qos1_publish: bool = True,
    ) -> int:
        """Publish and block for acknowledgement when required by QoS."""
        client = self._require_client()
        outbound_topic, outbound_properties = self._resolve_topic_alias(topic, properties)

        info = client.publish(
            outbound_topic,
            payload=payload,
            qos=qos,
            retain=retain,
            properties=outbound_properties,
        )
        if info.rc != mqtt.MQTT_ERR_SUCCESS:
            raise RuntimeError(self._build_publish_error_text(int(info.rc)))

        if qos == 0:
            if not wait_for_qos0_publish:
                return 0
            deadline = time.monotonic() + self._timeout_seconds
            while time.monotonic() < deadline:
                if info.is_published():
                    return 0
                time.sleep(0.01)
            raise TimeoutError("Timed out while waiting for QoS0 publish completion")

        if qos == 1 and not wait_for_qos1_publish:
            return int(info.mid)

        deadline = time.monotonic() + self._timeout_seconds
        while time.monotonic() < deadline:
            with self._mid_lock:
                if info.mid in self._published_mids:
                    return self._published_mids.pop(info.mid)
            time.sleep(0.01)

        raise TimeoutError("Timed out while waiting for PUBACK/PUBCOMP")

    def _build_publish_error_text(self, publish_rc: int) -> str:
        error_parts = [f"publish failed with rc={publish_rc}"]

        try:
            error_parts[0] = f"{error_parts[0]} ({mqtt.error_string(publish_rc)})"
        except Exception:
            pass

        queue_size_rc = int(getattr(mqtt, "MQTT_ERR_QUEUE_SIZE", 15))
        if publish_rc == queue_size_rc:
            queue_info = self._collect_client_queue_diagnostics()
            error_parts.append(
                "queue_full_origin=publisher-client-outgoing-queue"
                f" out_messages={queue_info['out_messages']}"
                f" max_queued_messages={queue_info['max_queued_messages']}"
                f" max_inflight_messages={queue_info['max_inflight_messages']}"
            )

        disconnect_event = self._disconnect_result
        if disconnect_event is None:
            # If publish raced with socket close, the disconnect callback may
            # arrive just after publish() returns rc!=0.
            self._disconnect_event.wait(timeout=min(0.5, self._timeout_seconds))
            disconnect_event = self._disconnect_result

        if disconnect_event is None:
            error_parts.append("disconnect=no-callback")
            return "; ".join(error_parts)

        source_text = "unknown"
        if disconnect_event.from_broker is True:
            source_text = "broker"
        elif disconnect_event.from_broker is False:
            source_text = "local"

        reason_value = int(disconnect_event.reason_code)
        reason_text = None
        if disconnect_event.from_broker is True:
            try:
                reason_text = mqtt.ReasonCode(mqtt.DISCONNECT >> 4, identifier=reason_value).getName()
            except Exception:
                reason_text = None
        if reason_text is None:
            try:
                reason_text = mqtt.error_string(reason_value)
            except Exception:
                reason_text = None

        disconnect_fragment = f"disconnect_source={source_text} reason=0x{reason_value:02X}"
        if reason_text:
            disconnect_fragment = f"{disconnect_fragment} ({reason_text})"
        error_parts.append(disconnect_fragment)
        return "; ".join(error_parts)

    def _collect_client_queue_diagnostics(self) -> dict[str, int]:
        client = self._client
        if client is None:
            return {
                "out_messages": -1,
                "max_queued_messages": -1,
                "max_inflight_messages": -1,
            }

        out_messages_value = getattr(client, "_out_messages", ())
        try:
            out_messages_count = int(len(out_messages_value))
        except Exception:
            out_messages_count = -1

        try:
            max_queued_messages = int(getattr(client, "_max_queued_messages", -1))
        except Exception:
            max_queued_messages = -1

        try:
            max_inflight_messages = int(getattr(client, "_max_inflight_messages", -1))
        except Exception:
            max_inflight_messages = -1

        return {
            "out_messages": out_messages_count,
            "max_queued_messages": max_queued_messages,
            "max_inflight_messages": max_inflight_messages,
        }

    def runtime_diagnostics(self) -> dict[str, int | bool | str]:
        client = self._client
        connected = bool(client is not None and client.is_connected())

        disconnect_source = "none"
        disconnect_reason = -1
        disconnect_from_broker = False
        disconnect_event = self._disconnect_result
        if disconnect_event is not None:
            disconnect_reason = int(disconnect_event.reason_code)
            disconnect_from_broker = bool(disconnect_event.from_broker)
            if disconnect_event.from_broker is True:
                disconnect_source = "broker"
            elif disconnect_event.from_broker is False:
                disconnect_source = "local"
            else:
                disconnect_source = "unknown"

        queue_info = self._collect_client_queue_diagnostics()

        inbound_queue_depth = -1
        try:
            inbound_queue_depth = int(self._inbound_messages.qsize())
        except Exception:
            inbound_queue_depth = -1

        with self._mid_lock:
            published_mid_cache = int(len(self._published_mids))

        return {
            "connected": connected,
            "disconnect_source": disconnect_source,
            "disconnect_reason": disconnect_reason,
            "disconnect_from_broker": disconnect_from_broker,
            "inbound_queue_depth": inbound_queue_depth,
            "published_mid_cache": published_mid_cache,
            "out_messages": int(queue_info["out_messages"]),
            "max_queued_messages": int(queue_info["max_queued_messages"]),
            "max_inflight_messages": int(queue_info["max_inflight_messages"]),
        }

    def drain_published_mids(self, limit: int | None = None) -> dict[int, int]:
        """Return currently completed publish mids and reason codes without blocking."""
        with self._mid_lock:
            if limit is None or limit >= len(self._published_mids):
                completed = dict(self._published_mids)
                self._published_mids.clear()
                return completed

            completed: dict[int, int] = {}
            for mid in list(self._published_mids.keys())[:limit]:
                completed[mid] = self._published_mids.pop(mid)
            return completed

    def subscribe(
        self,
        topic_filter: str,
        qos: int = 0,
        options: SubscribeOptions | None = None,
        subscription_id: int | None = None,
    ) -> list[int]:
        """Subscribe and return SUBACK reason codes."""
        client = self._require_client()

        subscribe_properties = None
        if subscription_id is not None:
            subscribe_properties = Properties(PacketTypes.SUBSCRIBE)
            setattr(subscribe_properties, "SubscriptionIdentifier", int(subscription_id))

        if options is not None:
            result, mid = client.subscribe(
                topic_filter,
                options=options,
                properties=subscribe_properties,
            )
        else:
            result, mid = client.subscribe(topic_filter, qos=qos, properties=subscribe_properties)
        if result != mqtt.MQTT_ERR_SUCCESS:
            raise RuntimeError(f"subscribe failed with rc={result}")

        return self._wait_for_mid_map(self._suback_mids, mid, "SUBACK")

    def unsubscribe(self, topic_filter: str) -> list[int]:
        """Unsubscribe and return UNSUBACK reason codes."""
        client = self._require_client()
        result, mid = client.unsubscribe(topic_filter)
        if result != mqtt.MQTT_ERR_SUCCESS:
            raise RuntimeError(f"unsubscribe failed with rc={result}")
        return self._wait_for_mid_map(self._unsuback_mids, mid, "UNSUBACK")

    def collect_messages(self, count: int, timeout: float) -> list[ReceivedMessage]:
        """Collect exactly count inbound publish messages or time out."""
        if count <= 0:
            return []

        messages: list[ReceivedMessage] = []
        deadline = time.monotonic() + timeout
        while len(messages) < count:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(
                    f"Timed out while waiting for {count} messages; got {len(messages)}"
                )
            try:
                messages.append(self._inbound_messages.get(timeout=remaining))
            except Empty as empty_error:
                raise TimeoutError(
                    f"Timed out while waiting for {count} messages; got {len(messages)}"
                ) from empty_error
        return messages

    def drain_available_messages(self, limit: int | None = None) -> list[ReceivedMessage]:
        """Return currently queued inbound messages without blocking."""
        if limit is not None and limit <= 0:
            return []

        messages: list[ReceivedMessage] = []
        while limit is None or len(messages) < limit:
            try:
                messages.append(self._inbound_messages.get_nowait())
            except Empty:
                break
        return messages

    def collect_message_for_topic(
        self,
        *,
        expected_topic: str,
        timeout: float,
        sample_limit: int = 5,
    ) -> ReceivedMessage:
        """Collect a single inbound message with expected topic, ignoring unrelated traffic."""
        deadline = time.monotonic() + max(0.2, timeout)
        sampled_topics: list[str] = []

        while time.monotonic() < deadline:
            remaining = max(0.05, deadline - time.monotonic())
            try:
                message = self.collect_messages(count=1, timeout=remaining)[0]
            except TimeoutError as timeout_error:
                sampled_text = ", ".join(sampled_topics) if sampled_topics else "none"
                raise TimeoutError(
                    "Timed out while waiting for expected topic "
                    f"{expected_topic!r}; sampled unrelated topics: {sampled_text}"
                ) from timeout_error

            if message.topic == expected_topic:
                return message
            if len(sampled_topics) < max(0, sample_limit):
                sampled_topics.append(message.topic)

        sampled_text = ", ".join(sampled_topics) if sampled_topics else "none"
        raise TimeoutError(
            "Timed out while waiting for expected topic "
            f"{expected_topic!r}; sampled unrelated topics: {sampled_text}"
        )

    def wait_for_disconnect(self, timeout: float) -> DisconnectEvent:
        """Wait for server initiated disconnect and return details."""
        if not self._disconnect_event.wait(timeout=timeout):
            raise TimeoutError("Timed out while waiting for server disconnect")
        if self._disconnect_result is None:
            raise RuntimeError("Disconnect event received without details")
        return self._disconnect_result

    def _create_client(self, client_id: str) -> None:
        callback_api_version = getattr(mqtt, "CallbackAPIVersion", None)
        if callback_api_version is None:
            self._client = mqtt.Client(client_id=client_id, protocol=self._protocol, transport=self._transport)
        else:
            self._client = mqtt.Client(
                callback_api_version=callback_api_version.VERSION2,
                client_id=client_id,
                protocol=self._protocol,
                transport=self._transport,
            )

        if self._max_inflight_messages is not None:
            self._client.max_inflight_messages_set(int(self._max_inflight_messages))

        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_publish = self._on_publish
        self._client.on_subscribe = self._on_subscribe
        self._client.on_unsubscribe = self._on_unsubscribe
        self._client.on_message = self._on_message
        self._client.on_log = self._on_log

    def _require_client(self) -> mqtt.Client:
        if self._client is None:
            raise RuntimeError("Client is not connected. Call connect() first.")
        return self._client

    def _wait_for_mid_map(self, mid_map: dict[int, list[int]], mid: int, packet_name: str) -> list[int]:
        deadline = time.monotonic() + self._timeout_seconds
        while time.monotonic() < deadline:
            with self._mid_lock:
                if mid in mid_map:
                    return mid_map.pop(mid)
            time.sleep(0.01)
        raise TimeoutError(f"Timed out while waiting for {packet_name}")

    def _resolve_topic_alias(
        self,
        topic: str,
        properties: Properties | None,
    ) -> tuple[str, Properties | None]:
        if self._topic_alias_maximum <= 0:
            return topic, properties

        publish_properties = properties
        if publish_properties is None:
            publish_properties = Properties(PacketTypes.PUBLISH)

        existing = self._topic_to_alias.get(topic)
        if existing is not None:
            setattr(publish_properties, "TopicAlias", existing)
            return "", publish_properties

        if self._next_topic_alias <= self._topic_alias_maximum:
            alias = self._next_topic_alias
            self._next_topic_alias += 1
            self._topic_to_alias[topic] = alias
            setattr(publish_properties, "TopicAlias", alias)

        return topic, publish_properties

    def _on_connect(self, _client: mqtt.Client, _userdata: Any, flags: Any, reason_code: Any, properties: Any = None) -> None:
        reason_code_int = int(getattr(reason_code, "value", reason_code))
        session_present = bool(getattr(flags, "session_present", False))
        if isinstance(flags, dict):
            session_present = bool(flags.get("session present", False))

        self._connack_result = ConnackResult(
            reason_code=reason_code_int,
            session_present=session_present,
            properties=properties,
        )
        self._connack_event.set()

    def _on_disconnect(
        self,
        _client: mqtt.Client,
        _userdata: Any,
        disconnect_flags: Any,
        reason_code: Any = 0,
        properties: Any = None,
    ) -> None:
        def _normalize_reason(value: Any) -> int:
            candidate = getattr(value, "value", value)
            try:
                return int(candidate)
            except (TypeError, ValueError):
                return 0

        reason_from_reason_arg = _normalize_reason(reason_code)
        reason_from_flags_arg = _normalize_reason(disconnect_flags)
        from_broker = getattr(disconnect_flags, "is_disconnect_packet_from_server", None)
        if from_broker is not None:
            from_broker = bool(from_broker)

        if os.environ.get("MQTT_INTEGRATION_DEBUG_DISCONNECT", "") == "1":
            print(
                "[mqtt_client disconnect]"
                f" flags={disconnect_flags!r}"
                f" reason={reason_code!r}"
                f" normalized_flags={reason_from_flags_arg}"
                f" normalized_reason={reason_from_reason_arg}",
                flush=True,
            )

        # Callback API v2 provides reason_code explicitly; keep it unless it is
        # the default 0 from API v1 compatibility invocation.
        reason_code_int = reason_from_reason_arg
        if reason_from_reason_arg == 0 and reason_from_flags_arg != 0:
            reason_code_int = reason_from_flags_arg
        if reason_code_int == 0 and self._disconnect_reason_from_log is not None:
            reason_code_int = self._disconnect_reason_from_log

        # Prefer the first non-zero reason code observed for this disconnect
        # cycle. Some clients emit a follow-up zero-value callback after a
        # server-initiated DISCONNECT with an error reason.
        existing_result = self._disconnect_result
        if existing_result is not None:
            if existing_result.reason_code != 0 and reason_code_int == 0:
                self._disconnect_event.set()
                return
            if existing_result.reason_code == 0 and reason_code_int != 0:
                self._disconnect_result = DisconnectEvent(
                    reason_code=reason_code_int,
                    properties=properties,
                    from_broker=from_broker,
                )
                self._disconnect_event.set()
                return
            if existing_result.reason_code != 0 and reason_code_int != 0:
                self._disconnect_event.set()
                return

        self._disconnect_result = DisconnectEvent(
            reason_code=reason_code_int,
            properties=properties,
            from_broker=from_broker,
        )
        self._disconnect_event.set()

    def _on_log(self, _client: mqtt.Client, _userdata: Any, _level: int, message: str) -> None:
        if os.environ.get("MQTT_INTEGRATION_DEBUG_DISCONNECT", "") == "1":
            print(f"[mqtt_client log] {message}", flush=True)

        log_match = _DISCONNECT_REASON_PATTERN.search(message)
        if log_match is None:
            return

        numbers = re.findall(r"\d+", log_match.group(1))
        if not numbers:
            return

        try:
            parsed_reason = int(numbers[-1])
        except ValueError:
            return

        self._disconnect_reason_from_log = parsed_reason

    def _on_publish(self, _client: mqtt.Client, _userdata: Any, mid: int, reason_code: Any = 0, _properties: Any = None) -> None:
        reason_code_int = int(getattr(reason_code, "value", reason_code))
        with self._mid_lock:
            self._published_mids[mid] = reason_code_int

    def _on_subscribe(
        self,
        _client: mqtt.Client,
        _userdata: Any,
        mid: int,
        reason_codes: Any,
        _properties: Any = None,
    ) -> None:
        normalized: list[int] = []
        for reason_code in reason_codes:
            normalized.append(int(getattr(reason_code, "value", reason_code)))
        with self._mid_lock:
            self._suback_mids[mid] = normalized

    def _on_unsubscribe(
        self,
        _client: mqtt.Client,
        _userdata: Any,
        mid: int,
        reason_codes: Any = (),
        _properties: Any = None,
    ) -> None:
        normalized: list[int] = []
        for reason_code in reason_codes:
            normalized.append(int(getattr(reason_code, "value", reason_code)))
        with self._mid_lock:
            self._unsuback_mids[mid] = normalized

    def _on_message(self, _client: mqtt.Client, _userdata: Any, message: mqtt.MQTTMessage) -> None:
        self._inbound_messages.put(
            ReceivedMessage(
                topic=message.topic,
                payload=bytes(message.payload),
                qos=message.qos,
                retain=bool(message.retain),
                dup=bool(getattr(message, "dup", False)),
                mid=message.mid,
                properties=getattr(message, "properties", None),
            )
        )
