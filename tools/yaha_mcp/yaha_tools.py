"""MCP tool definitions and handlers for the YAHA home automation system.

Each handler receives the validated argument dict and returns a JSON-serialisable
result. Errors are raised as YahaError or ValueError and reported as tool errors.
"""

from __future__ import annotations

import json
import os
import time
from typing import Any, Callable

from yaha_client import YahaClient, YahaError, default_system_name, load_systems
from yaha_history import (
    EventFilter,
    clip_value,
    coverage_note,
    matches_query,
    node_events,
    only_changes,
    reason_messages,
    select_topics,
    summarize,
    word_start_score,
)
from yaha_timeutil import LOCAL_TZ_NAME, format_local, parse_store_time, parse_user_time, utc_now

DEFAULT_CONFIG_KEYS = [
    "/automation/rules",
    "/zwave/settings",
    "/remoteservice/services",
    "/valueservice/values",
    "/yaha/gui/config/Computer",
    "/yaha/gui/config/Tablet",
]
GUI_CONFIG_KEYS = ["/yaha/gui/config/Computer", "/yaha/gui/config/Tablet"]
FILESTORE_MONITOR_TOPIC = "$MONITOR/FileStore/changed"
HISTORY_TOPIC_HARD_LIMIT = 300
EVENT_HARD_LIMIT = 2000
FIND_HARD_LIMIT = 1000
SECTION_HARD_LIMIT = 1000
CONFIG_MAX_CHARS_LIMIT = 200_000

SYSTEMS = load_systems()
DEFAULT_SYSTEM = default_system_name(SYSTEMS)
_CLIENTS: dict[str, YahaClient] = {}


def client_for(arguments: dict[str, Any]) -> YahaClient:
    """Returns the (cached) client for the requested system."""
    name = arguments.get("system") or DEFAULT_SYSTEM
    if name not in SYSTEMS:
        raise ValueError(f"unknown system '{name}'. Known: {', '.join(SYSTEMS)}")
    if name not in _CLIENTS:
        _CLIENTS[name] = YahaClient(name, SYSTEMS[name])
    return _CLIENTS[name]


def _bounded_int(arguments: dict[str, Any], key: str, default: int, upper: int) -> int:
    """Reads an integer argument clamped to [1, upper]."""
    value = arguments.get(key, default)
    try:
        number = int(value)
    except (TypeError, ValueError) as error:
        raise ValueError(f"{key} must be an integer") from error
    return max(1, min(number, upper))


def _node_output(node: dict[str, Any], include_reason: bool) -> dict[str, Any]:
    """Renders a current-value node with local time."""
    output: dict[str, Any] = {
        "topic": node.get("topic"),
        "value": clip_value(node.get("value")),
        "time": format_local(parse_store_time(node.get("time"))),
    }
    if include_reason:
        reasons = reason_messages(node.get("reason"))
        if reasons:
            output["reason"] = reasons
    return output


def _window(arguments: dict[str, Any], default_since: str | None) -> tuple[Any, Any]:
    """Parses since/until arguments into UTC datetimes (until defaults to now)."""
    now = utc_now()
    since = parse_user_time(arguments.get("since", default_since), now)
    until = parse_user_time(arguments.get("until"), now) or now
    if since is not None and since > until:
        raise ValueError("since must be before until")
    return since, until


def _selected_topics(client: YahaClient, arguments: dict[str, Any]) -> list[str]:
    """Resolves topics/prefix/query arguments into a topic list with a safety cap."""
    topics = arguments.get("topics") or []
    if isinstance(topics, str):
        topics = [topics]
    prefix = arguments.get("prefix")
    query = arguments.get("query")
    if not topics and prefix is None and not query:
        raise ValueError("give at least one of topics, prefix or query")
    names = [str(node.get("topic", "")) for node in client.all_topics()] if (prefix is not None or query) else []
    selected = select_topics(
        names, topics, prefix, query, bool(arguments.get("regex")), bool(arguments.get("include_monitor"))
    )
    limit = _bounded_int(arguments, "max_topics", 60, HISTORY_TOPIC_HARD_LIMIT)
    if len(selected) > limit:
        preview = ", ".join(selected[:15])
        raise ValueError(
            f"{len(selected)} topics selected, max_topics is {limit}. Narrow prefix/query or raise "
            f"max_topics (hard limit {HISTORY_TOPIC_HARD_LIMIT}). First topics: {preview}"
        )
    if not selected:
        raise ValueError("no topic matches the selection. Use yaha_find_topics to discover topic names")
    return selected


# ------------------------------------------------------------------ handlers


def tool_overview(arguments: dict[str, Any]) -> dict[str, Any]:
    """Root areas, topic counts and freshness of the message store."""
    client = client_for(arguments)
    nodes = client.all_topics(refresh=True)
    roots: dict[str, dict[str, Any]] = {}
    for node in nodes:
        topic = str(node.get("topic", ""))
        root = topic.split("/")[0]
        second = "/".join(topic.split("/")[:2])
        entry = roots.setdefault(root, {"topics": 0, "latest": None, "areas": {}})
        entry["topics"] += 1
        entry["areas"][second] = entry["areas"].get(second, 0) + 1
        moment = parse_store_time(node.get("time"))
        if moment and (entry["latest"] is None or moment > entry["latest"]):
            entry["latest"] = moment
    rendered = {
        root: {
            "topics": data["topics"],
            "latest_update": format_local(data["latest"]),
            "areas": dict(sorted(data["areas"].items(), key=lambda item: -item[1])[:25]),
        }
        for root, data in sorted(roots.items())
    }
    return {
        "system": client.name,
        "base_url": client.base_url,
        "known_systems": SYSTEMS,
        "timezone": LOCAL_TZ_NAME,
        "server_time_now": format_local(utc_now()),
        "topic_count": len(nodes),
        "roots": rendered,
    }


def tool_find_topics(arguments: dict[str, Any]) -> dict[str, Any]:
    """Searches topic names and returns current values plus GUI metadata."""
    client = client_for(arguments)
    nodes = client.all_topics()
    query = arguments.get("query")
    prefix = arguments.get("prefix")
    use_regex = bool(arguments.get("regex"))
    include_monitor = bool(arguments.get("include_monitor"))
    names = [str(node.get("topic", "")) for node in nodes]
    selected = set(select_topics(names, None, prefix if prefix is not None else "", query, use_regex, include_monitor))
    limit = _bounded_int(arguments, "limit", 100, FIND_HARD_LIMIT)
    gui = _gui_metadata(client)
    matches = []
    for node in nodes:
        if node.get("topic") not in selected:
            continue
        item = _node_output(node, False)
        meta = gui.get(str(node.get("topic")))
        if meta:
            item["gui"] = meta
        matches.append(item)
    if query and not use_regex:
        matches.sort(key=lambda item: -word_start_score(str(item["topic"]), query))
    return {"system": client.name, "match_count": len(matches), "truncated": len(matches) > limit, "topics": matches[:limit]}


def _gui_metadata(client: YahaClient) -> dict[str, dict[str, Any]]:
    """Merges GUI topic parameters (topicType, valueType, enumList, ...) of all GUI configs."""
    merged: dict[str, dict[str, Any]] = {}
    for key in GUI_CONFIG_KEYS:
        try:
            config = client.config_get(key)
        except YahaError:
            continue
        if not isinstance(config, dict):
            continue
        for topic, entry in config.items():
            parameters = entry.get("parameter") if isinstance(entry, dict) else None
            if isinstance(parameters, dict):
                merged.setdefault(topic, {}).update({name: value for name, value in parameters.items() if name != "topicRank"})
    return {topic: meta for topic, meta in merged.items() if meta}


def tool_get(arguments: dict[str, Any]) -> dict[str, Any]:
    """Current values for explicit topics or a subtree."""
    client = client_for(arguments)
    include_reason = arguments.get("include_reason", True)
    topics = arguments.get("topics") or []
    if isinstance(topics, str):
        topics = [topics]
    prefix = arguments.get("prefix")
    if not topics and prefix is None:
        raise ValueError("give topics or prefix")
    result: dict[str, Any] = {"system": client.name, "now": format_local(utc_now()), "nodes": []}
    missing = []
    for topic in topics:
        node = client.current(topic.strip())
        if node is None:
            missing.append(topic)
        else:
            result["nodes"].append(_node_output(node, include_reason))
    if prefix is not None:
        depth = _bounded_int(arguments, "depth", 1, 30) if arguments.get("depth", 1) != 0 else 0
        nodes = client.section(prefix, depth, reason=include_reason)
        limit = SECTION_HARD_LIMIT
        result["nodes"].extend(_node_output(node, include_reason) for node in nodes[:limit])
        result["truncated"] = len(nodes) > limit
    if missing:
        result["missing_topics"] = missing
    return result


def _collect_events(client: YahaClient, topics: list[str]) -> tuple[dict[str, list[Any]], list[str]]:
    """Fetches history per topic (one request each). Returns events by topic and missing topics."""
    by_topic: dict[str, list[Any]] = {}
    missing: list[str] = []
    for topic in topics:
        node = client.topic_with_history(topic)
        if node is None:
            missing.append(topic)
            continue
        by_topic[topic] = node_events(node)
    return by_topic, missing


def tool_history(arguments: dict[str, Any]) -> dict[str, Any]:
    """Merged, filtered timeline of stored messages across selected topics."""
    client = client_for(arguments)
    topics = _selected_topics(client, arguments)
    since, until = _window(arguments, "-24h")
    event_filter = EventFilter(
        since=since,
        until=until,
        value_equals=arguments.get("value_equals"),
        value_regex=arguments.get("value_regex"),
        reason_contains=arguments.get("reason_contains"),
    )
    by_topic, missing = _collect_events(client, topics)
    coverage: dict[str, str] = {}
    merged = []
    for topic, events in by_topic.items():
        series = only_changes(events) if arguments.get("changes_only") else events
        merged.extend(event for event in series if event_filter.accepts(event))
        note = coverage_note(events, since)
        if note:
            coverage[topic] = note
    newest_first = arguments.get("order", "desc") != "asc"
    merged.sort(key=lambda event: event.moment, reverse=newest_first)
    limit = _bounded_int(arguments, "limit", 200, EVENT_HARD_LIMIT)
    include_reason = arguments.get("include_reason", True)
    result: dict[str, Any] = {
        "system": client.name,
        "window": {"since": format_local(since), "until": format_local(until)},
        "topics_queried": len(topics),
        "events_matched": len(merged),
        "truncated": len(merged) > limit,
        "events": [event.to_output(include_reason) for event in merged[:limit]],
    }
    if coverage:
        result["coverage_warnings"] = coverage
    if missing:
        result["missing_topics"] = missing
    return result


def tool_history_summary(arguments: dict[str, Any]) -> dict[str, Any]:
    """Per-topic statistics for a time window."""
    client = client_for(arguments)
    topics = _selected_topics(client, arguments)
    since, until = _window(arguments, "-24h")
    by_topic, missing = _collect_events(client, topics)
    summaries = {topic: summarize(events, since, until) for topic, events in by_topic.items()}
    result: dict[str, Any] = {
        "system": client.name,
        "window": {"since": format_local(since), "until": format_local(until)},
        "topics": summaries,
    }
    if missing:
        result["missing_topics"] = missing
    return result


def tool_publish(arguments: dict[str, Any]) -> dict[str, Any]:
    """Publishes one message and optionally waits for the device state to react."""
    client = client_for(arguments)
    topic = str(arguments.get("topic", "")).strip()
    if not topic:
        raise ValueError("topic is required")
    if "value" not in arguments:
        raise ValueError("value is required")
    value = arguments["value"]
    qos = int(arguments.get("qos", 1))
    if qos not in (0, 1, 2):
        raise ValueError("qos must be 0, 1 or 2")
    retain = bool(arguments.get("retain", False))
    note = str(arguments.get("note") or "").strip()
    reason_message = "request by Claude (yaha MCP)" + (f": {note}" if note else "")
    state_topic = arguments.get("verify_topic") or (topic[: -len("/set")] if topic.endswith("/set") else topic)
    wait_seconds = max(0.0, min(float(arguments.get("verify_seconds", 5)), 60.0))
    before = client.current(state_topic)
    status = client.publish(topic, value, qos, retain, reason_message, utc_now().strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z")
    result: dict[str, Any] = {
        "system": client.name,
        "published": {"topic": topic, "value": value, "qos": qos, "retain": retain, "reason": reason_message},
        "http_status": status,
        "state_topic": state_topic,
        "state_before": _node_output(before, True) if before else None,
    }
    if wait_seconds <= 0:
        return result
    deadline = time.monotonic() + wait_seconds
    after = before
    while time.monotonic() < deadline:
        time.sleep(0.5)
        after = client.current(state_topic)
        if after and (before is None or after.get("time") != before.get("time")):
            break
    result["state_after"] = _node_output(after, True) if after else None
    result["state_updated"] = bool(after and (before is None or after.get("time") != before.get("time")))
    return result


def tool_config_list(arguments: dict[str, Any]) -> dict[str, Any]:
    """Lists known filestore configuration keys with size and top-level structure."""
    client = client_for(arguments)
    keys = set(DEFAULT_CONFIG_KEYS)
    extra = os.environ.get("YAHA_CONFIG_KEYS")
    if extra:
        keys.update(key.strip() for key in extra.split(",") if key.strip())
    try:
        monitor = client.topic_with_history(FILESTORE_MONITOR_TOPIC)
    except YahaError:
        monitor = None
    for event in node_events(monitor) if monitor else []:
        try:
            key_path = json.loads(str(event.value)).get("keyPath")
        except (json.JSONDecodeError, AttributeError):
            key_path = None
        if key_path:
            keys.add(str(key_path))
    listing = []
    for key in sorted(keys):
        try:
            value = client.config_get(key, refresh=True)
        except YahaError as error:
            listing.append({"key": key, "error": str(error)})
            continue
        entry: dict[str, Any] = {"key": key, "type": type(value).__name__, "chars": len(json.dumps(value, ensure_ascii=False))}
        if isinstance(value, dict):
            entry["top_level_keys"] = list(value)[:40]
        listing.append(entry)
    return {"system": client.name, "keys": listing}


def _json_path(value: Any, path: str) -> Any:
    """Navigates a slash separated path into nested dicts/lists."""
    current = value
    for part in [segment for segment in path.split("/") if segment]:
        if isinstance(current, dict) and part in current:
            current = current[part]
        elif isinstance(current, list) and part.isdigit() and int(part) < len(current):
            current = current[int(part)]
        else:
            raise ValueError(f"path segment '{part}' not found")
    return current


def _search_json(value: Any, needle: str, regex: bool, path: str = "") -> list[str]:
    """Returns paths of objects that directly contain a matching key or scalar value."""
    hits: list[str] = []
    if isinstance(value, dict):
        own = False
        for key, child in value.items():
            child_path = f"{path}/{key}"
            if matches_query(str(key), needle, regex):
                own = True
            if isinstance(child, (dict, list)):
                hits.extend(_search_json(child, needle, regex, child_path))
            elif matches_query(str(child), needle, regex):
                own = True
        if own:
            hits.insert(0, path or "/")
    elif isinstance(value, list):
        own = False
        for index, child in enumerate(value):
            if isinstance(child, (dict, list)):
                hits.extend(_search_json(child, needle, regex, f"{path}/{index}"))
            elif matches_query(str(child), needle, regex):
                own = True
        if own:
            hits.insert(0, path or "/")
    return hits


def tool_config_get(arguments: dict[str, Any]) -> dict[str, Any]:
    """Reads a filestore key, optionally narrowed by JSON path or search."""
    client = client_for(arguments)
    key = str(arguments.get("key", "")).strip()
    if not key:
        raise ValueError("key is required, see yaha_config_list")
    value = client.config_get(key, refresh=bool(arguments.get("refresh")))
    path = arguments.get("path")
    if path:
        value = _json_path(value, str(path))
    max_chars = _bounded_int(arguments, "max_chars", 20000, CONFIG_MAX_CHARS_LIMIT)
    search = arguments.get("search")
    if search:
        hit_paths = _search_json(value, str(search), bool(arguments.get("regex")))
        matches, used = [], 0
        for hit in hit_paths:
            content = _json_path(value, hit)
            size = len(json.dumps(content, ensure_ascii=False))
            if used + size > max_chars:
                return {"key": key, "path": path, "match_count": len(hit_paths), "truncated": True, "matches": matches}
            matches.append({"path": hit, "value": content})
            used += size
        return {"key": key, "path": path, "match_count": len(hit_paths), "truncated": False, "matches": matches}
    text = json.dumps(value, ensure_ascii=False)
    if len(text) > max_chars:
        top = list(value) if isinstance(value, dict) else None
        return {
            "key": key,
            "path": path,
            "truncated": True,
            "chars": len(text),
            "hint": "too large. Use path to drill down or search to filter",
            "top_level_keys": top,
        }
    return {"key": key, "path": path, "value": value}


# -------------------------------------------------------------- definitions

_SYSTEM_PROP = {
    "type": "string",
    "description": "YAHA system to talk to. Default is the production system.",
    "enum": list(SYSTEMS),
}
_SELECTION_PROPS = {
    "topics": {"type": "array", "items": {"type": "string"}, "description": "Exact topic names."},
    "prefix": {"type": "string", "description": "Select all topics below this prefix, e.g. 'ground/hallway'."},
    "query": {"type": "string", "description": "Select topics whose name contains all words, e.g. 'motion hallway'."},
    "regex": {"type": "boolean", "description": "Treat query as regular expression.", "default": False},
    "include_monitor": {"type": "boolean", "description": "Also select $MONITOR/ topics.", "default": False},
    "max_topics": {"type": "integer", "description": "Safety cap for selected topics (hard limit 300).", "default": 60},
}
_WINDOW_PROPS = {
    "since": {
        "type": "string",
        "description": "Window start. ISO (2026-10-16T18:30), date (2026-10-16, local midnight), keyword "
        "(today, yesterday, now) or relative (-2h, -7d). Naive times are local time.",
        "default": "-24h",
    },
    "until": {"type": "string", "description": "Window end, same formats. Default now."},
}

TOOLS: list[dict[str, Any]] = [
    {
        "name": "yaha_overview",
        "description": "Start here. Lists root areas of the YAHA message store with topic counts, sub-areas and "
        "latest update times, the configured systems and the local timezone.",
        "inputSchema": {"type": "object", "properties": {"system": _SYSTEM_PROP}},
        "handler": tool_overview,
    },
    {
        "name": "yaha_find_topics",
        "description": "Search topic names (all words must occur as substrings, case-insensitive, or regex). "
        "Matches where words start a word come first ('door' ranks 'front door' before 'outdoor'). Returns "
        "current value, last update time and GUI metadata (topicType, valueType, enumList). Use to discover "
        "which topics exist before reading history.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "query": {"type": "string", "description": "Words or regex matched against topic names. Empty lists all."},
                "prefix": {"type": "string", "description": "Restrict to topics below this prefix."},
                "regex": {"type": "boolean", "default": False},
                "include_monitor": {"type": "boolean", "default": False, "description": "Include $MONITOR/ topics."},
                "limit": {"type": "integer", "default": 100},
                "system": _SYSTEM_PROP,
            },
        },
        "handler": tool_find_topics,
    },
    {
        "name": "yaha_get",
        "description": "Current value, time and reason chain of exact topics or of a subtree (prefix + depth). "
        "No history. Cheap and always fresh.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "topics": {"type": "array", "items": {"type": "string"}},
                "prefix": {"type": "string"},
                "depth": {"type": "integer", "default": 1, "description": "Levels below prefix, 0 = prefix only."},
                "include_reason": {"type": "boolean", "default": True},
                "system": _SYSTEM_PROP,
            },
        },
        "handler": tool_get,
    },
    {
        "name": "yaha_history",
        "description": "Timeline of stored messages across selected topics within a time window, newest first. "
        "Filters: value_equals, value_regex, reason_contains, changes_only (drop repeated values). Each "
        "event has local time, topic, value and reason chain (why it happened, e.g. which rule fired). "
        "History is bounded per topic; coverage_warnings tell when stored history starts after 'since'.",
        "inputSchema": {
            "type": "object",
            "properties": {
                **_SELECTION_PROPS,
                **_WINDOW_PROPS,
                "value_equals": {"type": "string", "description": "Keep events with this value (case-insensitive)."},
                "value_regex": {"type": "string"},
                "reason_contains": {"type": "string", "description": "Keep events whose reason chain contains text."},
                "changes_only": {"type": "boolean", "default": False},
                "include_reason": {"type": "boolean", "default": True},
                "order": {"type": "string", "enum": ["desc", "asc"], "default": "desc"},
                "limit": {"type": "integer", "default": 200, "description": "Max events returned (hard limit 2000)."},
                "system": _SYSTEM_PROP,
            },
        },
        "handler": tool_history,
    },
    {
        "name": "yaha_history_summary",
        "description": "Per-topic statistics for a time window: event count, first/last, value at window start; "
        "numeric topics get min/max/avg with times; other topics get value counts, number of changes and "
        "time spent in each value (e.g. how long a light was on).",
        "inputSchema": {
            "type": "object",
            "properties": {**_SELECTION_PROPS, **_WINDOW_PROPS, "system": _SYSTEM_PROP},
        },
        "handler": tool_history_summary,
    },
    {
        "name": "yaha_publish",
        "description": "Publish one MQTT message through the browser interface (like the GUI does). This CHANGES "
        "the real house (switches devices, sets parameters). Only call after the user explicitly approved "
        "this exact topic and value. Commands usually go to '<device topic>/set'; the tool then waits for "
        "the state topic (topic without /set) to update and reports before/after.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "topic": {"type": "string"},
                "value": {"description": "Value to publish (string or number), e.g. 'on', 'off', 21.5."},
                "note": {"type": "string", "description": "Short reason text added to the reason chain."},
                "qos": {"type": "integer", "enum": [0, 1, 2], "default": 1},
                "retain": {"type": "boolean", "default": False},
                "verify_topic": {"type": "string", "description": "State topic to watch. Default topic without /set."},
                "verify_seconds": {"type": "number", "default": 5, "description": "0 disables waiting (max 60)."},
                "system": _SYSTEM_PROP,
            },
            "required": ["topic", "value"],
        },
        "handler": tool_publish,
    },
    {
        "name": "yaha_config_list",
        "description": "List configuration keys of the filestore (automation rules, zwave settings, GUI config, "
        "remote services, value service) with size and top-level keys.",
        "inputSchema": {"type": "object", "properties": {"system": _SYSTEM_PROP}},
        "handler": tool_config_list,
    },
    {
        "name": "yaha_config_get",
        "description": "Read one filestore configuration key. Use path to drill into JSON (slash separated, e.g. "
        "'motion/rules/homecoming' in /automation/rules; a rule named 'a/rules/b' in a reason chain "
        "lives at that path) or search to find objects containing a word (e.g. a topic name).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "key": {"type": "string", "description": "e.g. /automation/rules"},
                "path": {"type": "string"},
                "search": {"type": "string", "description": "Words (all must match) or regex with regex=true."},
                "regex": {"type": "boolean", "default": False},
                "max_chars": {"type": "integer", "default": 20000},
                "refresh": {"type": "boolean", "default": False},
                "system": _SYSTEM_PROP,
            },
            "required": ["key"],
        },
        "handler": tool_config_get,
    },
]

HANDLERS: dict[str, Callable[[dict[str, Any]], dict[str, Any]]] = {tool["name"]: tool["handler"] for tool in TOOLS}


def tool_definitions() -> list[dict[str, Any]]:
    """Returns MCP tool definitions without the internal handler reference."""
    return [{key: value for key, value in tool.items() if key != "handler"} for tool in TOOLS]
