"""Pure functions for topic selection, history event extraction, filtering and summaries.

No network access here; everything operates on message-store node dictionaries
of the form {"topic", "value", "time", "reason": [...], "history": [...]}.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from datetime import datetime
from typing import Any, Iterable

from yaha_timeutil import format_duration, format_local, parse_store_time

MONITOR_PREFIX = "$MONITOR/"
REASON_TEXT_LIMIT = 400
VALUE_TEXT_LIMIT = 500


@dataclass
class Event:
    """One stored message of one topic."""

    topic: str
    moment: datetime
    value: Any
    reasons: list[str] = field(default_factory=list)

    def to_output(self, include_reason: bool) -> dict[str, Any]:
        """Renders the event for tool output (local time, truncated texts)."""
        output: dict[str, Any] = {"time": format_local(self.moment), "topic": self.topic, "value": clip_value(self.value)}
        if include_reason and self.reasons:
            output["reason"] = [clip_text(reason, REASON_TEXT_LIMIT) for reason in self.reasons]
        return output


def clip_text(text: str, limit: int) -> str:
    """Truncates text to limit characters, marking the cut."""
    return text if len(text) <= limit else text[:limit] + f"...(+{len(text) - limit} chars)"


def clip_value(value: Any) -> Any:
    """Truncates long string values; other types pass through."""
    return clip_text(value, VALUE_TEXT_LIMIT) if isinstance(value, str) else value


def value_text(value: Any) -> str:
    """Canonical comparison text of a value (strings stripped, numbers without trailing .0)."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float) and value.is_integer():
        return str(int(value))
    return str(value).strip() if value is not None else ""


def as_number(value: Any) -> float | None:
    """Returns the value as float when it is numeric or a numeric string, else None."""
    if isinstance(value, bool) or value is None:
        return None
    if isinstance(value, (int, float)):
        return float(value)
    try:
        return float(str(value).strip())
    except ValueError:
        return None


def reason_messages(reason: Any) -> list[str]:
    """Extracts the message texts of a reason chain (oldest first)."""
    if not isinstance(reason, list):
        return []
    messages = []
    for entry in reason:
        if isinstance(entry, dict) and entry.get("message"):
            messages.append(str(entry["message"]))
        elif isinstance(entry, str):
            messages.append(entry)
    return messages


# ---------------------------------------------------------------- selection


def matches_query(topic: str, query: str | None, use_regex: bool) -> bool:
    """True when topic matches the query.

    Plain mode: every whitespace separated term must occur in the topic (case-insensitive).
    Regex mode: re.search with IGNORECASE.
    """
    if not query:
        return True
    if use_regex:
        return re.search(query, topic, re.IGNORECASE) is not None
    lowered = topic.lower()
    return all(term in lowered for term in query.lower().split())


def word_start_score(topic: str, query: str | None) -> int:
    """Counts query terms that occur at a word start in topic (e.g. 'door' in 'front door', not 'outdoor')."""
    if not query:
        return 0
    lowered = topic.lower()
    return sum(1 for term in query.lower().split() if re.search(r"(?<![a-z0-9])" + re.escape(term), lowered))


def under_prefix(topic: str, prefix: str | None) -> bool:
    """True when topic equals prefix or lies below it."""
    if not prefix:
        return True
    clean = prefix.strip().strip("/")
    return topic == clean or topic.startswith(clean + "/")


def select_topics(
    all_topics: Iterable[str],
    topics: list[str] | None = None,
    prefix: str | None = None,
    query: str | None = None,
    use_regex: bool = False,
    include_monitor: bool = False,
) -> list[str]:
    """Builds the sorted topic list from explicit topics plus prefix/query matches.

    Explicit topics are always included even when unknown. Prefix/query selection
    skips $MONITOR/ topics unless include_monitor is true or the prefix itself is
    inside $MONITOR.
    """
    selected: set[str] = {topic.strip() for topic in (topics or []) if topic and topic.strip()}
    if prefix is not None or query:
        monitor_allowed = include_monitor or (prefix or "").startswith("$MONITOR")
        for topic in all_topics:
            if not monitor_allowed and topic.startswith(MONITOR_PREFIX):
                continue
            if under_prefix(topic, prefix) and matches_query(topic, query, use_regex):
                selected.add(topic)
    return sorted(selected)


# ------------------------------------------------------------------- events


def node_events(node: dict[str, Any]) -> list[Event]:
    """Returns all events of one node (current value plus history), oldest first."""
    topic = str(node.get("topic", ""))
    events: list[Event] = []
    current_time = parse_store_time(node.get("time"))
    if current_time is not None and "value" in node:
        events.append(Event(topic, current_time, node.get("value"), reason_messages(node.get("reason"))))
    for entry in node.get("history") or []:
        moment = parse_store_time(entry.get("time"))
        if moment is None:
            continue
        events.append(Event(topic, moment, entry.get("value"), reason_messages(entry.get("reason"))))
    events.sort(key=lambda event: event.moment)
    return events


def only_changes(events: list[Event]) -> list[Event]:
    """Keeps only events whose value differs from the previous event of the same series."""
    result: list[Event] = []
    previous: str | None = None
    for event in events:
        text = value_text(event.value)
        if text != previous:
            result.append(event)
        previous = text
    return result


@dataclass
class EventFilter:
    """Filter criteria applied to events."""

    since: datetime | None = None
    until: datetime | None = None
    value_equals: str | None = None
    value_regex: str | None = None
    reason_contains: str | None = None

    def accepts(self, event: Event) -> bool:
        """True when the event passes all configured criteria."""
        if self.since is not None and event.moment < self.since:
            return False
        if self.until is not None and event.moment > self.until:
            return False
        text = value_text(event.value)
        if self.value_equals is not None and text.lower() != value_text(self.value_equals).lower():
            return False
        if self.value_regex and re.search(self.value_regex, text, re.IGNORECASE) is None:
            return False
        if self.reason_contains:
            needle = self.reason_contains.lower()
            if not any(needle in reason.lower() for reason in event.reasons):
                return False
        return True


def coverage_note(events: list[Event], since: datetime | None) -> str | None:
    """Warns when stored history starts after the requested window start."""
    if not events or since is None:
        return None
    oldest = events[0].moment
    if oldest > since:
        return f"history only available from {format_local(oldest)}"
    return None


# ------------------------------------------------------------------ summary


def _value_at(events: list[Event], moment: datetime) -> Event | None:
    """Returns the last event at or before moment (events sorted ascending)."""
    candidate = None
    for event in events:
        if event.moment <= moment:
            candidate = event
        else:
            break
    return candidate


def time_in_values(events: list[Event], start: datetime, end: datetime) -> dict[str, float]:
    """Seconds each value was active within [start, end] (step function semantics)."""
    durations: dict[str, float] = {}
    if end <= start:
        return durations
    state = _value_at(events, start)
    cursor = start
    for event in events:
        if event.moment <= start:
            continue
        if event.moment >= end:
            break
        if state is not None:
            key = value_text(state.value)
            durations[key] = durations.get(key, 0.0) + (event.moment - cursor).total_seconds()
        state = event
        cursor = event.moment
    if state is not None:
        key = value_text(state.value)
        durations[key] = durations.get(key, 0.0) + (end - cursor).total_seconds()
    return durations


def summarize(events: list[Event], since: datetime | None, until: datetime, value_limit: int = 10) -> dict[str, Any]:
    """Builds a statistical summary of one topic within the window.

    Numeric series (>= 80 % numeric values) report min/max/avg/last.
    Other series report value counts and time spent in each value.
    """
    window = [event for event in events if (since is None or event.moment >= since) and event.moment <= until]
    summary: dict[str, Any] = {"events_in_window": len(window)}
    note = coverage_note(events, since)
    if note:
        summary["coverage"] = note
    before = _value_at(events, since) if since is not None else None
    if before is not None and (not window or before is not window[0]):
        summary["value_at_window_start"] = before.to_output(False)
    if not window:
        return summary
    summary["first"] = window[0].to_output(False)
    summary["last"] = window[-1].to_output(False)
    numbers = [(event, as_number(event.value)) for event in window]
    numeric = [(event, number) for event, number in numbers if number is not None]
    if len(numeric) >= 0.8 * len(window):
        low = min(numeric, key=lambda pair: pair[1])
        high = max(numeric, key=lambda pair: pair[1])
        summary["numeric"] = {
            "min": low[1],
            "min_time": format_local(low[0].moment),
            "max": high[1],
            "max_time": format_local(high[0].moment),
            "avg": round(sum(number for _, number in numeric) / len(numeric), 3),
        }
        return summary
    counts: dict[str, int] = {}
    for event in window:
        key = value_text(event.value)
        counts[key] = counts.get(key, 0) + 1
    top = sorted(counts.items(), key=lambda item: -item[1])[:value_limit]
    summary["value_counts"] = {clip_text(key, 120): count for key, count in top}
    summary["value_changes"] = len(only_changes(window))
    start = since if since is not None else window[0].moment
    durations = time_in_values(events, start, until)
    top_durations = sorted(durations.items(), key=lambda item: -item[1])[:value_limit]
    summary["time_in_value"] = {clip_text(key, 120): format_duration(seconds) for key, seconds in top_durations}
    return summary
