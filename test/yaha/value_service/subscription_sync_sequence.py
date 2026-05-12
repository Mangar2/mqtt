from __future__ import annotations

import importlib.util
from pathlib import Path


def _load_base_module():
    module_path = Path(__file__).with_name("subscription_sync.py")
    spec = importlib.util.spec_from_file_location("valueservice_subscription_sync_base", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load base module from {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_BASE = _load_base_module()


def run_valueservice_existing_key_set_stays_stable(config) -> tuple[bool, str]:
    runtime = None
    subscriber = None
    try:
        _BASE._ensure_binaries()
        base_prefix = _BASE._make_topic_root("stable")
        runtime = _BASE._make_runtime(
            config=config,
            base_prefix=base_prefix,
            initial_values={f"{base_prefix}/key": "1"},
        )
        subscriber = _BASE._with_subscriber(runtime, config)

        if not _BASE._wait_for_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/key",
            payload_text="stable-1",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "existing key did not become active"

        _BASE._assert_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/key",
            payload_text="stable-2",
            timeout_seconds=2.0,
        )
        _BASE._assert_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/key",
            payload_text="stable-3",
            timeout_seconds=2.0,
        )
        return True, "existing key subscription remains stable across repeated /set updates"
    except Exception as error:
        return False, f"valueservice_existing_key_stable failed: {error}"
    finally:
        if subscriber is not None:
            subscriber.disconnect()
        _BASE._cleanup_runtime(runtime)


def run_valueservice_sequential_changes_keep_subscriptions_exact(config) -> tuple[bool, str]:
    runtime = None
    subscriber = None
    try:
        _BASE._ensure_binaries()
        base_prefix = _BASE._make_topic_root("sequence")
        runtime = _BASE._make_runtime(
            config=config,
            base_prefix=base_prefix,
            initial_values={f"{base_prefix}/a": "1", f"{base_prefix}/b": "2"},
        )
        subscriber = _BASE._with_subscriber(runtime, config)

        if not _BASE._wait_for_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/a",
            payload_text="a-ready",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "startup baseline a was not subscribed before sequence reload"
        if not _BASE._wait_for_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/b",
            payload_text="b-ready",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "startup baseline b was not subscribed before sequence reload"

        stages = [
            {
                "payload": {
                    f"{base_prefix}/b": "2",
                    f"{base_prefix}/c": "3",
                },
                "active": [f"{base_prefix}/b", f"{base_prefix}/c"],
                "inactive": [f"{base_prefix}/a"],
            },
            {
                "payload": {f"{base_prefix}/c": "3"},
                "active": [f"{base_prefix}/c"],
                "inactive": [f"{base_prefix}/a", f"{base_prefix}/b"],
            },
            {
                "payload": {
                    f"{base_prefix}/c": "3",
                    f"{base_prefix}/d": "4",
                },
                "active": [f"{base_prefix}/c", f"{base_prefix}/d"],
                "inactive": [f"{base_prefix}/a", f"{base_prefix}/b"],
            },
            {
                "payload": {},
                "active": [],
                "inactive": [f"{base_prefix}/a", f"{base_prefix}/b", f"{base_prefix}/c", f"{base_prefix}/d"],
            },
        ]

        for stage_index, stage in enumerate(stages, start=1):
            _BASE._filestore_post_json(
                host="127.0.0.1",
                port=runtime.filestore_port,
                key_path=runtime.values_key_path,
                payload=stage["payload"],
                timeout=max(2.0, config.timeout_seconds),
            )

            if stage["active"]:
                first_active = stage["active"][0]
                expected_snapshot = stage["payload"][first_active]
                if not _BASE._wait_for_topic_payload(
                    client=subscriber,
                    topic=first_active,
                    payload_text=expected_snapshot,
                    timeout_seconds=max(5.0, config.timeout_seconds),
                ):
                    return False, f"stage {stage_index}: snapshot for active key {first_active} not observed"

            for active_key in stage["active"]:
                if not _BASE._wait_for_set_roundtrip(
                    client=subscriber,
                    host=runtime.broker_host,
                    port=runtime.mqtt_port,
                    key_topic=active_key,
                    payload_text=f"stage-{stage_index}-active",
                    timeout_seconds=max(4.0, config.timeout_seconds),
                ):
                    return False, f"stage {stage_index}: active key {active_key} not subscribed"

            for inactive_key in stage["inactive"]:
                _BASE._assert_set_ignored(
                    client=subscriber,
                    host=runtime.broker_host,
                    port=runtime.mqtt_port,
                    key_topic=inactive_key,
                    payload_text=f"stage-{stage_index}-inactive",
                    timeout_seconds=1.0,
                )

        return True, "sequential key-set changes keep subscriptions exact after every step"
    except Exception as error:
        return False, f"valueservice_sequential_changes failed: {error}"
    finally:
        if subscriber is not None:
            subscriber.disconnect()
        _BASE._cleanup_runtime(runtime)


TEST_CASES = [
    {
        "name": "yaha/value_service/existing_key_set_stays_stable",
        "description": "Existing key subscription stays stable across repeated /set updates.",
        "run": run_valueservice_existing_key_set_stays_stable,
    },
    {
        "name": "yaha/value_service/sequential_changes_keep_subscriptions_exact",
        "description": "Sequential key-set transformations keep subscriptions exact at each stage.",
        "run": run_valueservice_sequential_changes_keep_subscriptions_exact,
    },
]
