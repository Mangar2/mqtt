"""Time parsing and formatting helpers for the YAHA MCP server.

All internal times are timezone-aware UTC datetimes. User-facing input accepts
absolute timestamps, dates, keywords and relative offsets; output is rendered
in the configured local timezone (default Europe/Berlin).
"""

from __future__ import annotations

import os
import re
from datetime import datetime, timedelta, timezone, tzinfo
from zoneinfo import ZoneInfo

LOCAL_TZ_NAME = os.environ.get("YAHA_TZ", "Europe/Berlin")
LOCAL_TZ: tzinfo = ZoneInfo(LOCAL_TZ_NAME)

_RELATIVE_RE = re.compile(r"^\s*([+-]?)\s*(\d+(?:\.\d+)?)\s*(s|sec|m|min|h|d|w)\s*(?:ago)?\s*$", re.IGNORECASE)
_UNIT_SECONDS = {"s": 1, "sec": 1, "m": 60, "min": 60, "h": 3600, "d": 86400, "w": 604800}
_DAY_KEYWORDS = {"today": 0, "heute": 0, "yesterday": -1, "gestern": -1, "vorgestern": -2}


def utc_now() -> datetime:
    """Returns the current time as timezone-aware UTC datetime."""
    return datetime.now(timezone.utc)


def parse_store_time(text: str | None) -> datetime | None:
    """Parses an ISO-8601 time string emitted by the message store.

    @param text ISO-8601 string such as 2026-10-17T10:01:59.000Z.
    @returns Aware UTC datetime or None when text is empty or invalid.
    """
    if not text:
        return None
    try:
        parsed = datetime.fromisoformat(text.replace("Z", "+00:00"))
    except ValueError:
        return None
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc)


def parse_user_time(text: str | None, now: datetime | None = None) -> datetime | None:
    """Parses a user supplied time expression.

    Accepted forms:
      - None or "" -> None (open bound)
      - "now"
      - keywords "today"/"heute", "yesterday"/"gestern", "vorgestern" -> local midnight of that day
      - relative offsets "-2h", "30m", "-7d", "1w", "3h ago" (always into the past)
      - date "2026-10-16" -> local midnight
      - local datetime without offset "2026-10-16 18:30" or "2026-10-16T18:30:00"
      - ISO-8601 with offset or Z

    @param text Time expression.
    @param now Reference time (UTC), defaults to current time.
    @returns Aware UTC datetime or None.
    @raises ValueError when the expression cannot be parsed.
    """
    if text is None:
        return None
    value = str(text).strip()
    if not value:
        return None
    reference = now or utc_now()
    lowered = value.lower()
    if lowered in ("now", "jetzt"):
        return reference
    if lowered in _DAY_KEYWORDS:
        local_midnight = reference.astimezone(LOCAL_TZ).replace(hour=0, minute=0, second=0, microsecond=0)
        local_day = local_midnight + timedelta(days=_DAY_KEYWORDS[lowered])
        return _local_wall_time_to_utc(local_day.replace(tzinfo=None))
    relative = _RELATIVE_RE.match(value)
    if relative:
        amount = float(relative.group(2)) * _UNIT_SECONDS[relative.group(3).lower()]
        return reference - timedelta(seconds=amount)
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise ValueError(
            f"cannot parse time '{value}'. Use ISO (2026-10-16T18:30), date (2026-10-16), "
            "keyword (now, today, yesterday) or relative (-2h, -7d)"
        ) from error
    if parsed.tzinfo is None:
        return _local_wall_time_to_utc(parsed)
    return parsed.astimezone(timezone.utc)


def _local_wall_time_to_utc(naive_local: datetime) -> datetime:
    """Interprets a naive datetime as local wall-clock time and converts it to UTC."""
    return naive_local.replace(tzinfo=LOCAL_TZ).astimezone(timezone.utc)


def format_local(moment: datetime | None) -> str | None:
    """Formats an aware datetime as compact local time with offset and weekday.

    Example: "2026-10-17 12:01:59 +02:00 Sat".
    """
    if moment is None:
        return None
    local = moment.astimezone(LOCAL_TZ)
    offset = local.strftime("%z")
    offset_text = f"{offset[:3]}:{offset[3:]}" if offset else ""
    return f"{local.strftime('%Y-%m-%d %H:%M:%S')} {offset_text} {local.strftime('%a')}".strip()


def format_duration(seconds: float) -> str:
    """Formats a duration in seconds as compact text like 2d 3h 4m or 45s."""
    total = int(round(abs(seconds)))
    days, rest = divmod(total, 86400)
    hours, rest = divmod(rest, 3600)
    minutes, secs = divmod(rest, 60)
    parts = []
    if days:
        parts.append(f"{days}d")
    if hours:
        parts.append(f"{hours}h")
    if minutes:
        parts.append(f"{minutes}m")
    if not parts:
        parts.append(f"{secs}s")
    return " ".join(parts)
