"""Integration tests for Topic Matching (3.1 – 3.3)."""

from __future__ import annotations

import importlib.util
import socket
import uuid
from pathlib import Path


def _load_helper(module_name: str):
    helper_path = Path(__file__).resolve().parents[1] / "helpers" / f"{module_name}.py"
    spec = importlib.util.spec_from_file_location(f"integration_helper_{module_name}", helper_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load helper module {module_name} from {helper_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_assertions_module = _load_helper("assertions")
_broker_module = _load_helper("broker")
_mqtt_client_module = _load_helper("mqtt_client")

assert_connack = _assertions_module.assert_connack
assert_message = _assertions_module.assert_message
assert_no_message = _assertions_module.assert_no_message
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def _start_isolated_broker():
    overrides: dict[str, object] = {
        "network.mqtt_port": _find_free_port(),
        "network.ws_port": 0,
        "broker.allow_anonymous": True,
    }
    process = start_broker(overrides)
    return "127.0.0.1", int(overrides["network.mqtt_port"]), process


def _run_prefix() -> str:
    return f"t/{uuid.uuid4().hex[:8]}"


# ─── 3.1 Exact Topic Match ────────────────────────────────────────────────────

def run_3_1_1_exact_match_delivered(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pfx = _run_prefix()
        sub_filter = f"{pfx}/a/b/c"
        pub_topic = f"{pfx}/a/b/c"
        payload = b"exact-match"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-1-1"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-1-1"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, payload, qos=0)
            msgs = sub.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(msgs[0], topic=pub_topic, payload=payload, qos=0, retain=False)
        return True, "3.1.1 exact topic match delivered"
    except Exception as e:
        return False, f"3.1.1 failed: {e}"
    finally:
        stop_broker(process)


def run_3_1_2_exact_mismatch_not_delivered(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pfx = _run_prefix()
        sub_filter = f"{pfx}/a/b/c"
        pub_topic = f"{pfx}/a/b/d"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-1-2"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-1-2"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, b"no-match", qos=0)
            assert_no_message(sub, timeout=min(1.5, config.timeout_seconds))
        return True, "3.1.2 different last segment not delivered"
    except Exception as e:
        return False, f"3.1.2 failed: {e}"
    finally:
        stop_broker(process)


def run_3_1_3_exact_shorter_not_delivered(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pfx = _run_prefix()
        sub_filter = f"{pfx}/a/b/c"
        pub_topic = f"{pfx}/a/b"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-1-3"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-1-3"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, b"shorter", qos=0)
            assert_no_message(sub, timeout=min(1.5, config.timeout_seconds))
        return True, "3.1.3 shorter topic not delivered to longer exact subscription"
    except Exception as e:
        return False, f"3.1.3 failed: {e}"
    finally:
        stop_broker(process)


def run_3_1_4_exact_longer_not_delivered(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pfx = _run_prefix()
        sub_filter = f"{pfx}/a/b/c"
        pub_topic = f"{pfx}/a/b/c/d"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-1-4"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-1-4"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, b"longer", qos=0)
            assert_no_message(sub, timeout=min(1.5, config.timeout_seconds))
        return True, "3.1.4 longer topic not delivered to shorter exact subscription"
    except Exception as e:
        return False, f"3.1.4 failed: {e}"
    finally:
        stop_broker(process)


# ─── 3.2 Single-Level Wildcard (+) ───────────────────────────────────────────

def run_3_2_1_plus_wildcard_match(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pfx = _run_prefix()
        sub_filter = f"{pfx}/a/+/c"
        pub_topic = f"{pfx}/a/b/c"
        payload = b"plus-match"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-2-1"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-2-1"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, payload, qos=0)
            msgs = sub.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(msgs[0], topic=pub_topic, payload=payload, qos=0, retain=False)
        return True, "3.2.1 a/+/c matches a/b/c"
    except Exception as e:
        return False, f"3.2.1 failed: {e}"
    finally:
        stop_broker(process)


def run_3_2_2_plus_wildcard_different_segment(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pfx = _run_prefix()
        sub_filter = f"{pfx}/a/+/c"
        pub_topic = f"{pfx}/a/x/c"
        payload = b"plus-x"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-2-2"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-2-2"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, payload, qos=0)
            msgs = sub.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(msgs[0], topic=pub_topic, payload=payload, qos=0, retain=False)
        return True, "3.2.2 a/+/c matches a/x/c"
    except Exception as e:
        return False, f"3.2.2 failed: {e}"
    finally:
        stop_broker(process)


def run_3_2_3_plus_wildcard_no_match_different_last(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pfx = _run_prefix()
        sub_filter = f"{pfx}/a/+/c"
        pub_topic = f"{pfx}/a/b/d"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-2-3"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-2-3"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, b"no-match", qos=0)
            assert_no_message(sub, timeout=min(1.5, config.timeout_seconds))
        return True, "3.2.3 a/+/c does not match a/b/d"
    except Exception as e:
        return False, f"3.2.3 failed: {e}"
    finally:
        stop_broker(process)


def run_3_2_4_plus_wildcard_at_start(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pfx = _run_prefix()
        sub_filter = f"+/{pfx}/b/c"
        pub_topic = f"a/{pfx}/b/c"
        payload = b"plus-start"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-2-4"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-2-4"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, payload, qos=0)
            msgs = sub.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(msgs[0], topic=pub_topic, payload=payload, qos=0, retain=False)
        return True, "3.2.4 +/b/c matches a/b/c"
    except Exception as e:
        return False, f"3.2.4 failed: {e}"
    finally:
        stop_broker(process)


def run_3_2_5_plus_wildcard_two_levels(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pfx = _run_prefix()
        sub_filter = f"{pfx}/a/+"
        pub_topic = f"{pfx}/a/b"
        payload = b"two-level"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-2-5"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-2-5"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, payload, qos=0)
            msgs = sub.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(msgs[0], topic=pub_topic, payload=payload, qos=0, retain=False)
        return True, "3.2.5 a/+ matches a/b"
    except Exception as e:
        return False, f"3.2.5 failed: {e}"
    finally:
        stop_broker(process)


def run_3_2_6_plus_wildcard_single_level(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pfx = _run_prefix()
        # Use a unique single-level topic: "t-<hex>" (no slash in the segment itself)
        sub_filter = "+"
        pub_topic = f"t{pfx.replace('/', '-')}"
        payload = b"single-level"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-2-6"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-2-6"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, payload, qos=0)
            msgs = sub.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(msgs[0], topic=pub_topic, payload=payload, qos=0, retain=False)
        return True, "3.2.6 + matches single-level topic"
    except Exception as e:
        return False, f"3.2.6 failed: {e}"
    finally:
        stop_broker(process)


def run_3_2_7_plus_wildcard_no_match_extra_level(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pfx = _run_prefix()
        sub_filter = f"{pfx}/a/+/c"
        pub_topic = f"{pfx}/a/b/c/d"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-2-7"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-2-7"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, b"extra-level", qos=0)
            assert_no_message(sub, timeout=min(1.5, config.timeout_seconds))
        return True, "3.2.7 a/+/c does not match a/b/c/d"
    except Exception as e:
        return False, f"3.2.7 failed: {e}"
    finally:
        stop_broker(process)


# ─── 3.3 Multi-Level Wildcard (#) ────────────────────────────────────────────

def run_3_3_1_hash_wildcard_single_child(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pfx = _run_prefix()
        sub_filter = f"{pfx}/a/#"
        pub_topic = f"{pfx}/a/b"
        payload = b"hash-single"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-3-1"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-3-1"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, payload, qos=0)
            msgs = sub.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(msgs[0], topic=pub_topic, payload=payload, qos=0, retain=False)
        return True, "3.3.1 a/# matches a/b"
    except Exception as e:
        return False, f"3.3.1 failed: {e}"
    finally:
        stop_broker(process)


def run_3_3_2_hash_wildcard_deep_hierarchy(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pfx = _run_prefix()
        sub_filter = f"{pfx}/a/#"
        pub_topic = f"{pfx}/a/b/c/d"
        payload = b"hash-deep"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-3-2"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-3-2"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, payload, qos=0)
            msgs = sub.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(msgs[0], topic=pub_topic, payload=payload, qos=0, retain=False)
        return True, "3.3.2 a/# matches a/b/c/d"
    except Exception as e:
        return False, f"3.3.2 failed: {e}"
    finally:
        stop_broker(process)


def run_3_3_3_hash_wildcard_parent_topic(config) -> tuple[bool, str]:
    """a/# must also match 'a' itself (the parent of the wildcard level)."""
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pfx = _run_prefix()
        sub_filter = f"{pfx}/a/#"
        pub_topic = f"{pfx}/a"
        payload = b"hash-parent"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-3-3"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-3-3"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, payload, qos=0)
            msgs = sub.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(msgs[0], topic=pub_topic, payload=payload, qos=0, retain=False)
        return True, "3.3.3 a/# matches a (parent topic)"
    except Exception as e:
        return False, f"3.3.3 failed: {e}"
    finally:
        stop_broker(process)


def run_3_3_4_hash_root_matches_all(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pfx = _run_prefix()
        pub_topic = f"{pfx}/some/deep/topic"
        payload = b"root-hash"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-3-4"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe("#", qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-3-4"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, payload, qos=0)
            msgs = sub.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(msgs[0], topic=pub_topic, payload=payload, qos=0, retain=False)
        return True, "3.3.4 # matches any regular topic"
    except Exception as e:
        return False, f"3.3.4 failed: {e}"
    finally:
        stop_broker(process)


def run_3_3_5_hash_root_excludes_sys(config) -> tuple[bool, str]:
    """# must NOT deliver messages with $SYS/... topics (MQTT-4.7.2-1)."""
    process = None
    try:
        host, port, process = _start_isolated_broker()
        sys_topic = "$SYS/test/topic_match"
        payload = b"sys-excluded"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-3-5"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe("#", qos=0)
            # Attempt to publish to a $SYS topic; broker may or may not accept it.
            # Regardless, the # subscriber must not receive it.
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-3-5"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(sys_topic, payload, qos=0)
            # Collect any messages that arrive; none of them may have a $SYS topic
            import time
            time.sleep(min(1.0, config.timeout_seconds * 0.5))
            msgs = sub.collect_messages(count=0, timeout=min(1.0, config.timeout_seconds))
            sys_msgs = [m for m in msgs if m.topic.startswith("$SYS")]
            if sys_msgs:
                return False, f"3.3.5 # subscriber received {len(sys_msgs)} $SYS message(s): {sys_msgs[0].topic}"
        return True, "3.3.5 # subscriber received no $SYS messages"
    except Exception as e:
        return False, f"3.3.5 failed: {e}"
    finally:
        stop_broker(process)


# ─── 3.4 System Topics ($SYS) ────────────────────────────────────────────────

def run_3_4_1_sys_subscription_receives_messages(config) -> tuple[bool, str]:
    """$SYS/# subscription must receive broker-published $SYS messages."""
    process = None
    try:
        port = _find_free_port()
        overrides: dict = {
            "network.mqtt_port": port,
            "network.ws_port": 0,
            "broker.allow_anonymous": True,
            "monitoring.sys_topic_interval": 1,
        }
        process = start_broker(overrides)
        host = "127.0.0.1"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-4-1"), clean_start=True), reason_code=0x00, session_present=False)
            codes = sub.subscribe("$SYS/#", qos=0)
            if codes[0] > 2:
                return False, f"3.4.1 SUBACK rejected $SYS/# with reason code 0x{codes[0]:02X}"
            msgs = sub.collect_messages(count=1, timeout=min(5.0, config.timeout_seconds))
            if not msgs:
                return False, "3.4.1 no $SYS messages received within timeout"
            non_sys = [m for m in msgs if not m.topic.startswith("$SYS")]
            if non_sys:
                return False, f"3.4.1 received non-$SYS message on $SYS/# subscription: {non_sys[0].topic}"
        return True, f"3.4.1 $SYS/# subscription received {len(msgs)} $SYS message(s)"
    except Exception as e:
        return False, f"3.4.1 failed: {e}"
    finally:
        stop_broker(process)


def run_3_4_2_hash_does_not_receive_sys(config) -> tuple[bool, str]:
    """# wildcard must NOT deliver $SYS topics per MQTT-4.7.2-1."""
    process = None
    try:
        port = _find_free_port()
        overrides: dict = {
            "network.mqtt_port": port,
            "network.ws_port": 0,
            "broker.allow_anonymous": True,
            "monitoring.sys_topic_interval": 1,
        }
        process = start_broker(overrides)
        host = "127.0.0.1"
        import time
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-4-2"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe("#", qos=0)
            time.sleep(min(2.0, config.timeout_seconds * 0.4))
            try:
                msgs = sub.collect_messages(count=1, timeout=0.5)
                sys_msgs = [m for m in msgs if m.topic.startswith("$SYS")]
                if sys_msgs:
                    return False, f"3.4.2 # subscriber received $SYS message: {sys_msgs[0].topic}"
            except TimeoutError:
                pass
        return True, "3.4.2 # subscriber received no $SYS messages"
    except Exception as e:
        return False, f"3.4.2 failed: {e}"
    finally:
        stop_broker(process)


def run_3_4_3_plus_does_not_match_sys_prefix(config) -> tuple[bool, str]:
    """+/broker/uptime must NOT match $SYS/broker/uptime — $ prefix excluded from +."""
    process = None
    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-4-3"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe("+/broker/uptime", qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-4-3"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish("$SYS/broker/uptime", b"42", qos=0)
            assert_no_message(sub, timeout=min(1.5, config.timeout_seconds))
        return True, "3.4.3 +/broker/uptime did not match $SYS/broker/uptime"
    except Exception as e:
        return False, f"3.4.3 failed: {e}"
    finally:
        stop_broker(process)


def run_3_4_4_client_cannot_publish_to_sys(config) -> tuple[bool, str]:
    """Client publish to $SYS topic must not be routed to any subscriber."""
    process = None
    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-4-4"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe("$SYS/#", qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-4-4"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish("$SYS/broker/clients/connected", b"99", qos=0)
            assert_no_message(sub, timeout=min(1.5, config.timeout_seconds))
        return True, "3.4.4 client publish to $SYS not routed to subscribers"
    except Exception as e:
        return False, f"3.4.4 failed: {e}"
    finally:
        stop_broker(process)


# ─── 3.5 Combined Wildcards ───────────────────────────────────────────────────

def run_3_5_1_combined_wildcards_match(config) -> tuple[bool, str]:
    """sport/+/player/# must match sport/tennis/player/ranking."""
    process = None
    try:
        host, port, process = _start_isolated_broker()
        sub_filter = "sport/+/player/#"
        pub_topic = "sport/tennis/player/ranking"
        payload = b"combined-wildcards"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-5-1"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-5-1"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, payload, qos=0)
            msgs = sub.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(msgs[0], topic=pub_topic, payload=payload, qos=0, retain=False)
        return True, "3.5.1 sport/+/player/# matched sport/tennis/player/ranking"
    except Exception as e:
        return False, f"3.5.1 failed: {e}"
    finally:
        stop_broker(process)


def run_3_5_2_triple_plus_match_and_no_extra_level(config) -> tuple[bool, str]:
    """+/+/+ must match a/b/c but NOT a/b/c/d."""
    process = None
    try:
        host, port, process = _start_isolated_broker()
        uid = uuid.uuid4().hex[:8]
        sub_filter = "+/+/+"
        topic_match = f"{uid}/b/c"
        topic_no_match = f"{uid}/b/c/d"
        payload = b"triple-plus"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-5-2"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe(sub_filter, qos=0)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-5-2"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(topic_match, payload, qos=0)
            msgs = sub.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(msgs[0], topic=topic_match, payload=payload, qos=0, retain=False)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub2:
                assert_connack(pub2.connect(host, port, client_id=_unique_client_id("pub2-3-5-2"), clean_start=True), reason_code=0x00, session_present=False)
                pub2.publish(topic_no_match, b"extra-level", qos=0)
            assert_no_message(sub, timeout=min(1.5, config.timeout_seconds))
        return True, "3.5.2 +/+/+ matched a/b/c and did not match a/b/c/d"
    except Exception as e:
        return False, f"3.5.2 failed: {e}"
    finally:
        stop_broker(process)


def run_3_5_3_overlapping_subscriptions_with_identifiers(config) -> tuple[bool, str]:
    """Overlapping subscriptions with Subscription Identifiers — identifier forwarded in PUBLISH."""
    process = None
    try:
        host, port, process = _start_isolated_broker()
        pub_topic = "sport/tennis/player/ranking"
        payload = b"overlap"
        with MqttClient(timeout_seconds=config.timeout_seconds) as sub:
            assert_connack(sub.connect(host, port, client_id=_unique_client_id("sub-3-5-3"), clean_start=True), reason_code=0x00, session_present=False)
            sub.subscribe("sport/+/player/#", qos=0, subscription_id=1)
            sub.subscribe("sport/tennis/#", qos=0, subscription_id=2)
            with MqttClient(timeout_seconds=config.timeout_seconds) as pub:
                assert_connack(pub.connect(host, port, client_id=_unique_client_id("pub-3-5-3"), clean_start=True), reason_code=0x00, session_present=False)
                pub.publish(pub_topic, payload, qos=0)
            msgs = sub.collect_messages(count=1, timeout=config.timeout_seconds)
            if not msgs:
                return False, "3.5.3 no message received"
            sub_ids: list[int] = []
            for msg in msgs:
                props = getattr(msg, "properties", None)
                if props is not None:
                    sid = getattr(props, "SubscriptionIdentifier", None)
                    if sid is not None:
                        if isinstance(sid, list):
                            sub_ids.extend(sid)
                        else:
                            sub_ids.append(int(sid))
            if not sub_ids:
                return False, "3.5.3 no SubscriptionIdentifier in received PUBLISH properties"
            if not any(sid in (1, 2) for sid in sub_ids):
                return False, f"3.5.3 unexpected SubscriptionIdentifier values: {sub_ids}"
        return True, f"3.5.3 overlapping subscriptions delivered with SubscriptionIdentifier(s): {sub_ids}"
    except Exception as e:
        return False, f"3.5.3 failed: {e}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "topic/exact_match_delivered",
        "description": "3.1.1 Subscribe a/b/c, publish a/b/c — message delivered",
        "run": run_3_1_1_exact_match_delivered,
    },
    {
        "name": "topic/exact_mismatch_not_delivered",
        "description": "3.1.2 Subscribe a/b/c, publish a/b/d — message NOT delivered",
        "run": run_3_1_2_exact_mismatch_not_delivered,
    },
    {
        "name": "topic/exact_shorter_not_delivered",
        "description": "3.1.3 Subscribe a/b/c, publish a/b — message NOT delivered",
        "run": run_3_1_3_exact_shorter_not_delivered,
    },
    {
        "name": "topic/exact_longer_not_delivered",
        "description": "3.1.4 Subscribe a/b/c, publish a/b/c/d — message NOT delivered",
        "run": run_3_1_4_exact_longer_not_delivered,
    },
    {
        "name": "topic/plus_wildcard_match",
        "description": "3.2.1 Subscribe a/+/c, publish a/b/c — message delivered",
        "run": run_3_2_1_plus_wildcard_match,
    },
    {
        "name": "topic/plus_wildcard_different_segment",
        "description": "3.2.2 Subscribe a/+/c, publish a/x/c — message delivered",
        "run": run_3_2_2_plus_wildcard_different_segment,
    },
    {
        "name": "topic/plus_wildcard_no_match_different_last",
        "description": "3.2.3 Subscribe a/+/c, publish a/b/d — message NOT delivered",
        "run": run_3_2_3_plus_wildcard_no_match_different_last,
    },
    {
        "name": "topic/plus_wildcard_at_start",
        "description": "3.2.4 Subscribe +/b/c, publish a/b/c — message delivered",
        "run": run_3_2_4_plus_wildcard_at_start,
    },
    {
        "name": "topic/plus_wildcard_two_levels",
        "description": "3.2.5 Subscribe a/+, publish a/b — message delivered",
        "run": run_3_2_5_plus_wildcard_two_levels,
    },
    {
        "name": "topic/plus_wildcard_single_level",
        "description": "3.2.6 Subscribe +, publish single-level topic — message delivered",
        "run": run_3_2_6_plus_wildcard_single_level,
    },
    {
        "name": "topic/plus_wildcard_no_match_extra_level",
        "description": "3.2.7 Subscribe a/+/c, publish a/b/c/d — message NOT delivered",
        "run": run_3_2_7_plus_wildcard_no_match_extra_level,
    },
    {
        "name": "topic/hash_wildcard_single_child",
        "description": "3.3.1 Subscribe a/#, publish a/b — message delivered",
        "run": run_3_3_1_hash_wildcard_single_child,
    },
    {
        "name": "topic/hash_wildcard_deep_hierarchy",
        "description": "3.3.2 Subscribe a/#, publish a/b/c/d — message delivered",
        "run": run_3_3_2_hash_wildcard_deep_hierarchy,
    },
    {
        "name": "topic/hash_wildcard_parent_topic",
        "description": "3.3.3 Subscribe a/#, publish a — message delivered",
        "run": run_3_3_3_hash_wildcard_parent_topic,
    },
    {
        "name": "topic/hash_root_matches_all",
        "description": "3.3.4 Subscribe #, publish any topic — message delivered",
        "run": run_3_3_4_hash_root_matches_all,
    },
    {
        "name": "topic/hash_root_excludes_sys",
        "description": "3.3.5 Subscribe #, publish $SYS topic — message NOT delivered",
        "run": run_3_3_5_hash_root_excludes_sys,
    },
    {
        "name": "topic/sys_subscription_receives_messages",
        "description": "3.4.1 Subscribe $SYS/# — receives $SYS messages",
        "run": run_3_4_1_sys_subscription_receives_messages,
    },
    {
        "name": "topic/hash_does_not_receive_sys",
        "description": "3.4.2 Subscribe # — does NOT receive $SYS messages",
        "run": run_3_4_2_hash_does_not_receive_sys,
    },
    {
        "name": "topic/plus_does_not_match_sys_prefix",
        "description": "3.4.3 Subscribe +/broker/uptime — does NOT match $SYS/broker/uptime",
        "run": run_3_4_3_plus_does_not_match_sys_prefix,
    },
    {
        "name": "topic/client_cannot_publish_to_sys",
        "description": "3.4.4 Client publish to $SYS topic — not routed to subscribers",
        "run": run_3_4_4_client_cannot_publish_to_sys,
    },
    {
        "name": "topic/combined_wildcards_match",
        "description": "3.5.1 Subscribe sport/+/player/#, publish sport/tennis/player/ranking — delivered",
        "run": run_3_5_1_combined_wildcards_match,
    },
    {
        "name": "topic/triple_plus_match_and_no_extra_level",
        "description": "3.5.2 Subscribe +/+/+, publish a/b/c — delivered; publish a/b/c/d — NOT delivered",
        "run": run_3_5_2_triple_plus_match_and_no_extra_level,
    },
    {
        "name": "topic/overlapping_subscriptions_with_identifiers",
        "description": "3.5.3 Overlapping subscriptions with Subscription Identifiers — identifier in PUBLISH",
        "run": run_3_5_3_overlapping_subscriptions_with_identifiers,
    },
]
