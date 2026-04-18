"""Blocking MQTT v5 client helper for integration tests.

This module wraps paho-mqtt with test-oriented convenience methods.
"""

from __future__ import annotations

from queue import Empty, Queue
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
        mid: int,
        properties: Properties | None,
    ) -> None:
        self.topic = topic
        self.payload = payload
        self.qos = qos
        self.retain = retain
        self.mid = mid
        self.properties = properties


class DisconnectEvent:
    """Server initiated disconnect details."""

    def __init__(self, reason_code: int, properties: Properties | None) -> None:
        self.reason_code = reason_code
        self.properties = properties


class MqttClient:
    """High-level MQTT client with blocking methods for integration tests."""

    def __init__(
        self,
        *,
        client_id: str = "",
        protocol: int | None = None,
        transport: str = "tcp",
        timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS,
    ) -> None:
        self._ensure_dependency_available()
        self._client_id = client_id
        self._protocol = mqtt.MQTTv5 if protocol is None else protocol
        self._transport = transport
        self._timeout_seconds = timeout_seconds

        self._client: mqtt.Client | None = None
        self._connack_event = Event()
        self._disconnect_event = Event()
        self._connack_result: ConnackResult | None = None
        self._disconnect_result: DisconnectEvent | None = None

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

        self._disconnect_event.clear()
        self._client.disconnect(reasoncode=reason_code, properties=properties)

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
            raise RuntimeError(f"publish failed with rc={info.rc}")

        if qos == 0:
            return 0

        deadline = time.monotonic() + self._timeout_seconds
        while time.monotonic() < deadline:
            with self._mid_lock:
                if info.mid in self._published_mids:
                    return self._published_mids.pop(info.mid)
            time.sleep(0.01)

        raise TimeoutError("Timed out while waiting for PUBACK/PUBCOMP")

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

        request = options if options is not None else qos
        result, mid = client.subscribe(topic_filter, qos=request, properties=subscribe_properties)
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

        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_publish = self._on_publish
        self._client.on_subscribe = self._on_subscribe
        self._client.on_unsubscribe = self._on_unsubscribe
        self._client.on_message = self._on_message

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
        reason_code_int = int(getattr(reason_code, "value", reason_code))
        if isinstance(disconnect_flags, int):
            reason_code_int = int(getattr(disconnect_flags, "value", disconnect_flags))

        self._disconnect_result = DisconnectEvent(reason_code=reason_code_int, properties=properties)
        self._disconnect_event.set()

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
                mid=message.mid,
                properties=getattr(message, "properties", None),
            )
        )
