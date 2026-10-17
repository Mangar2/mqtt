"""Unit tests for the YAHA MCP server (see TEST_SPEC.md). No real YAHA system is contacted."""

from __future__ import annotations

import json
import os
import sys
import threading
import unittest
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


class MockState:
    """Shared state of the mock YAHA HTTP interface."""

    def __init__(self) -> None:
        self.nodes: dict[str, dict[str, Any]] = {}
        self.configs: dict[str, Any] = {}
        self.store_requests: list[dict[str, Any]] = []
        self.publish_requests: list[dict[str, Any]] = []
        self.publish_status = 204

    def query(self, body: dict[str, Any]) -> list[dict[str, Any]]:
        prefix = body.get("topic", "")
        level = int(body.get("levelAmount", 1))
        result = []
        for topic, node in sorted(self.nodes.items()):
            if prefix == "":
                depth = len(topic.split("/"))
            elif topic == prefix:
                depth = 0
            elif topic.startswith(prefix + "/"):
                depth = len(topic[len(prefix) + 1 :].split("/"))
            else:
                continue
            if depth > level:
                continue
            copy = dict(node)
            if not body.get("history"):
                copy.pop("history", None)
            result.append(copy)
        return result


STATE = MockState()


class MockHandler(BaseHTTPRequestHandler):
    """Mock of nginx routes /store, /publish and /kvstore."""

    def log_message(self, *_: Any) -> None:  # silence
        return

    def _reply(self, status: int, payload: Any) -> None:
        data = b"" if payload is None else json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self) -> None:  # noqa: N802 - http.server API
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))) or b"{}")
        if self.path == "/store":
            STATE.store_requests.append(body)
            self._reply(200, {"payload": STATE.query(body)})
        elif self.path == "/publish":
            STATE.publish_requests.append(body)
            if STATE.publish_status != 204:
                self._reply(STATE.publish_status, {"error": "downstream failed"})
                return
            topic = body["topic"]
            state_topic = topic[: -len("/set")] if topic.endswith("/set") else topic
            now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
            STATE.nodes[state_topic] = {"topic": state_topic, "value": body["value"], "time": now, "reason": body["reason"]}
            self._reply(204, None)
        else:
            self._reply(404, {"error": "unknown"})

    def do_GET(self) -> None:  # noqa: N802 - http.server API
        if self.path.startswith("/kvstore/"):
            key = "/" + self.path[len("/kvstore/") :].replace("%20", " ")
            if key in STATE.configs:
                self._reply(200, STATE.configs[key])
                return
        self._reply(404, {"error": "not found"})


SERVER = ThreadingHTTPServer(("127.0.0.1", 0), MockHandler)
threading.Thread(target=SERVER.serve_forever, daemon=True).start()
os.environ["YAHA_SYSTEMS"] = json.dumps({"mock": f"http://127.0.0.1:{SERVER.server_address[1]}"})
os.environ.pop("YAHA_DEFAULT_SYSTEM", None)

import yaha_mcp_server  # noqa: E402
import yaha_tools  # noqa: E402
from yaha_client import YahaError  # noqa: E402
from yaha_history import (  # noqa: E402
    Event,
    EventFilter,
    coverage_note,
    node_events,
    only_changes,
    select_topics,
    summarize,
    time_in_values,
    word_start_score,
)
from yaha_timeutil import format_duration, format_local, parse_user_time  # noqa: E402

UTC = timezone.utc


def iso(moment: datetime) -> str:
    """Formats like the message store."""
    return moment.astimezone(UTC).strftime("%Y-%m-%dT%H:%M:%S.000Z")


def make_node(topic: str, values: list[tuple[datetime, Any, str]]) -> dict[str, Any]:
    """Builds a store node; values oldest first, last one becomes current."""
    *older, (current_time, current_value, current_reason) = values
    history = [{"time": iso(t), "value": v, "reason": [{"message": r, "timestamp": iso(t)}]} for t, v, r in reversed(older)]
    return {
        "topic": topic,
        "value": current_value,
        "time": iso(current_time),
        "reason": [{"message": current_reason, "timestamp": iso(current_time)}],
        "history": history,
    }


class TimeUtilTest(unittest.TestCase):
    REF = datetime(2026, 10, 17, 10, 0, 0, tzinfo=UTC)

    def test_relative_offsets(self) -> None:
        self.assertEqual(parse_user_time("-2h", self.REF), self.REF - timedelta(hours=2))
        self.assertEqual(parse_user_time("30m", self.REF), self.REF - timedelta(minutes=30))
        self.assertEqual(parse_user_time("1w", self.REF), self.REF - timedelta(days=7))
        self.assertEqual(parse_user_time("3h ago", self.REF), self.REF - timedelta(hours=3))

    def test_keywords_local_midnight(self) -> None:
        self.assertEqual(parse_user_time("today", self.REF), datetime(2026, 10, 16, 22, 0, tzinfo=UTC))
        self.assertEqual(parse_user_time("yesterday", self.REF), datetime(2026, 10, 15, 22, 0, tzinfo=UTC))
        self.assertEqual(parse_user_time("gestern", self.REF), datetime(2026, 10, 15, 22, 0, tzinfo=UTC))
        self.assertEqual(parse_user_time("now", self.REF), self.REF)
        self.assertIsNone(parse_user_time("", self.REF))

    def test_date_and_naive_local(self) -> None:
        self.assertEqual(parse_user_time("2026-10-16", self.REF), datetime(2026, 10, 15, 22, 0, tzinfo=UTC))
        self.assertEqual(parse_user_time("2026-10-16 18:30", self.REF), datetime(2026, 10, 16, 16, 30, tzinfo=UTC))
        self.assertEqual(parse_user_time("2026-12-24T18:00:00", self.REF), datetime(2026, 12, 24, 17, 0, tzinfo=UTC))

    def test_iso_with_offset(self) -> None:
        self.assertEqual(parse_user_time("2026-10-16T18:30:00Z", self.REF), datetime(2026, 10, 16, 18, 30, tzinfo=UTC))

    def test_invalid_raises(self) -> None:
        with self.assertRaises(ValueError):
            parse_user_time("next tuesday", self.REF)

    def test_format_local(self) -> None:
        self.assertEqual(format_local(datetime(2026, 10, 17, 10, 1, 59, tzinfo=UTC)), "2026-10-17 12:01:59 +02:00 Sat")
        self.assertIsNone(format_local(None))

    def test_format_duration(self) -> None:
        self.assertEqual(format_duration(45), "45s")
        self.assertEqual(format_duration(3720), "1h 2m")
        self.assertEqual(format_duration(93784), "1d 2h 3m")


class HistoryTest(unittest.TestCase):
    T0 = datetime(2026, 10, 17, 8, 0, tzinfo=UTC)

    def series(self, values: list[Any], step_minutes: int = 10, topic: str = "x") -> list[Event]:
        return [Event(topic, self.T0 + timedelta(minutes=index * step_minutes), value, [f"r{index}"]) for index, value in enumerate(values)]

    def test_select_topics_prefix_query_monitor(self) -> None:
        names = ["ground/hall/motion", "ground/hallway/motion", "ground/hall/light", "$MONITOR/ground/hall/motion", "first/x"]
        self.assertEqual(select_topics(names, prefix="ground/hall"), ["ground/hall/light", "ground/hall/motion"])
        self.assertEqual(select_topics(names, query="motion hall"), ["ground/hall/motion", "ground/hallway/motion"])
        self.assertIn("$MONITOR/ground/hall/motion", select_topics(names, query="motion hall", include_monitor=True))
        self.assertEqual(select_topics(names, prefix="$MONITOR"), ["$MONITOR/ground/hall/motion"])
        self.assertEqual(select_topics(names, query=r"^first/", use_regex=True), ["first/x"])
        self.assertEqual(select_topics(names, topics=["unknown/topic"]), ["unknown/topic"])

    def test_word_start_score(self) -> None:
        self.assertEqual(word_start_score("ground/hallway/front door/state", "door"), 1)
        self.assertEqual(word_start_score("outdoor/garden/temperature", "door"), 0)
        self.assertEqual(word_start_score("ground/hallway/motion sensor", "hall motion"), 2)
        self.assertEqual(word_start_score("x", None), 0)

    def test_node_events_sorted_with_current(self) -> None:
        node = make_node("a", [(self.T0, "on", "r1"), (self.T0 + timedelta(minutes=5), "off", "r2"), (self.T0 + timedelta(minutes=9), "on", "r3")])
        events = node_events(node)
        self.assertEqual([event.value for event in events], ["on", "off", "on"])
        self.assertEqual(events[-1].reasons, ["r3"])
        self.assertEqual(events[0].moment, self.T0)

    def test_only_changes(self) -> None:
        self.assertEqual([e.value for e in only_changes(self.series(["on", "on", "off", "off", "on"]))], ["on", "off", "on"])
        self.assertEqual(len(only_changes(self.series([1, 1.0, "1"]))), 1)

    def test_event_filter(self) -> None:
        events = self.series(["on", "off", "ON", "dim"])
        window = EventFilter(since=self.T0 + timedelta(minutes=5), until=self.T0 + timedelta(minutes=25))
        self.assertEqual([e.value for e in events if window.accepts(e)], ["off", "ON"])
        self.assertEqual([e.value for e in events if EventFilter(value_equals="on").accepts(e)], ["on", "ON"])
        self.assertEqual([e.value for e in events if EventFilter(value_regex="^d").accepts(e)], ["dim"])
        self.assertEqual([e.value for e in events if EventFilter(reason_contains="R3").accepts(e)], ["dim"])

    def test_coverage_note(self) -> None:
        events = self.series(["a", "b"])
        self.assertIsNotNone(coverage_note(events, self.T0 - timedelta(hours=1)))
        self.assertIsNone(coverage_note(events, self.T0 + timedelta(minutes=1)))
        self.assertIsNone(coverage_note([], self.T0))

    def test_time_in_values(self) -> None:
        events = self.series(["off", "on", "off"], step_minutes=30)
        durations = time_in_values(events, self.T0 + timedelta(minutes=15), self.T0 + timedelta(minutes=75))
        self.assertEqual(durations, {"off": 15 * 60 + 15 * 60, "on": 30 * 60})
        self.assertEqual(time_in_values(events, self.T0, self.T0), {})

    def test_summarize_numeric_and_enum(self) -> None:
        numeric = summarize(self.series(["10.5", "12", "9"]), self.T0, self.T0 + timedelta(hours=1))
        self.assertEqual(numeric["numeric"]["min"], 9.0)
        self.assertEqual(numeric["numeric"]["max"], 12.0)
        self.assertEqual(numeric["events_in_window"], 3)
        enum = summarize(self.series(["awake", "absent", "awake"], step_minutes=60), self.T0 + timedelta(minutes=30), self.T0 + timedelta(hours=3))
        self.assertEqual(enum["value_counts"], {"absent": 1, "awake": 1})
        self.assertEqual(enum["value_at_window_start"]["value"], "awake")
        self.assertEqual(enum["time_in_value"], {"awake": "1h 30m", "absent": "1h"})
        empty = summarize(self.series(["a"]), self.T0 + timedelta(hours=5), self.T0 + timedelta(hours=6))
        self.assertEqual(empty["events_in_window"], 0)


class ServerProtocolTest(unittest.TestCase):
    def test_initialize_and_list(self) -> None:
        response = yaha_mcp_server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2024-11-05"}})
        self.assertEqual(response["result"]["protocolVersion"], "2024-11-05")
        tools = yaha_mcp_server.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})["result"]["tools"]
        self.assertEqual(len(tools), 8)
        self.assertTrue(all("handler" not in tool and "inputSchema" in tool for tool in tools))

    def test_unknown_method_and_notification(self) -> None:
        response = yaha_mcp_server.handle({"jsonrpc": "2.0", "id": 3, "method": "foo/bar"})
        self.assertEqual(response["error"]["code"], -32601)
        self.assertIsNone(yaha_mcp_server.handle({"jsonrpc": "2.0", "method": "notifications/initialized"}))

    def test_tool_error_is_reported(self) -> None:
        response = yaha_mcp_server.handle({"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "yaha_history", "arguments": {}}})
        self.assertTrue(response["result"]["isError"])
        self.assertIn("topics, prefix or query", response["result"]["content"][0]["text"])


class MockSystemTest(unittest.TestCase):
    def setUp(self) -> None:
        yaha_tools._CLIENTS.clear()
        now = datetime.now(UTC).replace(microsecond=0)
        self.now = now
        STATE.nodes = {
            "home/hall/motion": make_node("home/hall/motion", [(now - timedelta(hours=h), v, f"sensor {v}") for h, v in [(5, 0), (4, 1), (3, 1), (2, 0), (1, 1)]]),
            "home/presence": make_node("home/presence", [(now - timedelta(hours=6), "absent", "Rule: leaving"), (now - timedelta(hours=1), "awake", "Rule: motion/rules/homecoming")]),
            "home/lamp": make_node("home/lamp", [(now - timedelta(hours=2), "off", "user")]),
            "$MONITOR/FileStore/changed": make_node("$MONITOR/FileStore/changed", [(now, json.dumps({"keyPath": "/extra/key"}), "fs")]),
        }
        STATE.configs = {
            "/automation/rules": {"motion": {"rules": {"homecoming": {"check": "home/presence = absent", "value": "awake"}}}},
            "/extra/key": {"a": 1},
            "/yaha/gui/config/Computer": {"home/presence": {"parameter": {"topicType": "Parameter", "topicRank": "1"}}},
        }
        STATE.store_requests.clear()
        STATE.publish_requests.clear()
        STATE.publish_status = 204

    def test_section_never_requests_history(self) -> None:
        yaha_tools.client_for({}).all_topics(refresh=True)
        self.assertEqual(STATE.store_requests[-1], {"topic": "", "levelAmount": 30, "history": False, "reason": False, "time": True})

    def test_topic_history_single_topic_requests(self) -> None:
        yaha_tools.tool_history({"prefix": "home", "since": "-1d"})
        history_requests = [request for request in STATE.store_requests if request["history"]]
        self.assertEqual(len(history_requests), 3)
        self.assertTrue(all(request["levelAmount"] == 0 for request in history_requests))

    def test_history_tool_filters_and_orders(self) -> None:
        result = yaha_tools.tool_history({"topics": ["home/hall/motion"], "since": "-10h", "changes_only": True, "value_equals": "1", "order": "asc"})
        self.assertEqual([event["value"] for event in result["events"]], [1, 1])
        self.assertIn("home/hall/motion", result["coverage_warnings"])
        self.assertEqual(result["events"][0]["reason"], ["sensor 1"])
        presence = yaha_tools.tool_history({"query": "presence", "reason_contains": "homecoming", "include_reason": False})
        self.assertEqual(presence["events_matched"], 1)
        self.assertNotIn("reason", presence["events"][0])

    def test_topic_cap_guard(self) -> None:
        with self.assertRaises(ValueError) as context:
            yaha_tools.tool_history({"prefix": "home", "max_topics": 2})
        self.assertIn("3 topics selected", str(context.exception))
        with self.assertRaises(ValueError):
            yaha_tools.tool_history({"query": "nothing-matches-this"})

    def test_publish_and_verify(self) -> None:
        result = yaha_tools.tool_publish({"topic": "home/lamp/set", "value": "on", "note": "test", "verify_seconds": 3})
        body = STATE.publish_requests[-1]
        self.assertEqual(body["topic"], "home/lamp/set")
        self.assertEqual(body["qos"], 1)
        self.assertFalse(body["retain"])
        self.assertEqual(body["reason"][0]["message"], "request by Claude (yaha MCP): test")
        self.assertTrue(result["state_updated"])
        self.assertEqual(result["state_after"]["value"], "on")
        self.assertEqual(result["state_before"]["value"], "off")
        with self.assertRaises(ValueError):
            yaha_tools.tool_publish({"topic": "home/lamp/set"})

    def test_publish_http_error(self) -> None:
        STATE.publish_status = 500
        with self.assertRaises(YahaError):
            yaha_tools.tool_publish({"topic": "home/lamp/set", "value": "on", "verify_seconds": 0})

    def test_config_get_path_search_and_missing(self) -> None:
        by_path = yaha_tools.tool_config_get({"key": "/automation/rules", "path": "motion/rules/homecoming"})
        self.assertEqual(by_path["value"]["value"], "awake")
        found = yaha_tools.tool_config_get({"key": "/automation/rules", "search": "home/presence"})
        self.assertEqual([match["path"] for match in found["matches"]], ["/motion/rules/homecoming"])
        with self.assertRaises(YahaError):
            yaha_tools.tool_config_get({"key": "/does/not/exist"})
        with self.assertRaises(ValueError):
            yaha_tools.tool_config_get({"key": "/automation/rules", "path": "motion/missing"})

    def test_config_list_discovers_monitor_keys(self) -> None:
        listing = {entry["key"]: entry for entry in yaha_tools.tool_config_list({})["keys"]}
        self.assertEqual(listing["/extra/key"]["top_level_keys"], ["a"])
        self.assertIn("error", listing["/zwave/settings"])

    def test_get_and_find_topics(self) -> None:
        result = yaha_tools.tool_get({"topics": ["home/presence", "no/such"]})
        self.assertEqual(result["nodes"][0]["value"], "awake")
        self.assertEqual(result["missing_topics"], ["no/such"])
        subtree = yaha_tools.tool_get({"prefix": "home", "depth": 2, "include_reason": False})
        self.assertEqual(len(subtree["nodes"]), 3)
        found = yaha_tools.tool_find_topics({"query": "presence"})
        self.assertEqual(found["topics"][0]["gui"], {"topicType": "Parameter"})
        overview = yaha_tools.tool_overview({})
        self.assertEqual(overview["roots"]["home"]["topics"], 3)
        STATE.nodes["home/a outdoor"] = make_node("home/a outdoor", [(self.now, 1, "x")])
        STATE.nodes["home/z door"] = make_node("home/z door", [(self.now, 1, "x")])
        yaha_tools._CLIENTS.clear()
        ranked = yaha_tools.tool_find_topics({"query": "door"})
        self.assertEqual([item["topic"] for item in ranked["topics"]], ["home/z door", "home/a outdoor"])


if __name__ == "__main__":
    unittest.main()
