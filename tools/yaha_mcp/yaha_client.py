"""HTTP client for the YAHA browser interface (nginx on the Raspberry Pi).

Endpoints used (all behind nginx port 80):
  - POST /store            message store section query (msgstore, port 8090)
  - POST /publish          publish one MQTT message (httpmqtt, port 8183)
  - GET  /kvstore/<key>    read one filestore key (filestore, port 8210)

Safety rule: history is only ever requested for exactly one topic per request
(levelAmount 0). A history request for a whole subtree makes msgstore build the
entire decompressed history in memory; on 2026-10-17 a root query with history
pushed msgstore to ~800 MB and the kernel OOM killer terminated it.
"""

from __future__ import annotations

import json
import os
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

DEFAULT_SYSTEMS = {"yahapi": "http://192.168.0.4", "yaha2": "http://192.168.0.183"}
FULL_TREE_DEPTH = 30
HTTP_TIMEOUT_SECONDS = float(os.environ.get("YAHA_HTTP_TIMEOUT", "20"))
MAX_RESPONSE_BYTES = 20_000_000
TOPIC_LIST_CACHE_SECONDS = 30.0
HISTORY_CACHE_SECONDS = 20.0
CONFIG_CACHE_SECONDS = 300.0
HISTORY_REQUEST_PAUSE_SECONDS = 0.02


class YahaError(Exception):
    """Raised for any communication or protocol failure with a YAHA system."""


def load_systems() -> dict[str, str]:
    """Returns the configured system name -> base URL map.

    Override with env YAHA_SYSTEMS='{"name": "http://host"}'.
    """
    raw = os.environ.get("YAHA_SYSTEMS")
    if not raw:
        return dict(DEFAULT_SYSTEMS)
    try:
        parsed = json.loads(raw)
    except json.JSONDecodeError as error:
        raise YahaError(f"YAHA_SYSTEMS is not valid JSON: {error}") from error
    return {str(name): str(url).rstrip("/") for name, url in parsed.items()}


def default_system_name(systems: dict[str, str]) -> str:
    """Returns the default system name (env YAHA_DEFAULT_SYSTEM or yahapi or first entry)."""
    configured = os.environ.get("YAHA_DEFAULT_SYSTEM")
    if configured and configured in systems:
        return configured
    if "yahapi" in systems:
        return "yahapi"
    return next(iter(systems))


class _TimedCache:
    """Small thread-safe TTL cache."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._entries: dict[Any, tuple[float, Any]] = {}

    def get(self, key: Any, ttl: float) -> Any:
        with self._lock:
            entry = self._entries.get(key)
            if entry is None or time.monotonic() - entry[0] > ttl:
                return None
            return entry[1]

    def put(self, key: Any, value: Any) -> None:
        with self._lock:
            self._entries[key] = (time.monotonic(), value)

    def drop(self, key: Any) -> None:
        with self._lock:
            self._entries.pop(key, None)


class YahaClient:
    """Read/write access to one YAHA system via its HTTP browser interface."""

    def __init__(self, name: str, base_url: str) -> None:
        self.name = name
        self.base_url = base_url.rstrip("/")
        self._cache = _TimedCache()

    # ------------------------------------------------------------------ HTTP

    def _request(self, method: str, path: str, body: Any = None, headers: dict[str, str] | None = None) -> tuple[int, bytes]:
        """Performs one HTTP request and returns (status, body bytes)."""
        url = self.base_url + path
        data = None
        all_headers = dict(headers or {})
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            all_headers["Content-Type"] = "application/json"
        request = urllib.request.Request(url, data=data, method=method, headers=all_headers)
        try:
            with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
                payload = response.read(MAX_RESPONSE_BYTES + 1)
                status = response.status
        except urllib.error.HTTPError as error:
            with error:
                payload = error.read(64_000)
                status = error.code
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            raise YahaError(f"{self.name}: {method} {path} failed: {error}") from error
        if len(payload) > MAX_RESPONSE_BYTES:
            raise YahaError(f"{self.name}: response of {path} exceeds {MAX_RESPONSE_BYTES} bytes")
        return status, payload

    def _store_query(self, topic: str, level_amount: int, history: bool, reason: bool) -> list[dict[str, Any]]:
        """POST /store section query. Returns the node list."""
        body = {"topic": topic, "levelAmount": level_amount, "history": history, "reason": reason, "time": True}
        status, payload = self._request("POST", "/store", body)
        if status != 200:
            raise YahaError(f"{self.name}: /store returned HTTP {status}: {payload[:300]!r}")
        try:
            parsed = json.loads(payload)
        except json.JSONDecodeError as error:
            raise YahaError(f"{self.name}: /store returned invalid JSON") from error
        nodes = parsed.get("payload", []) if isinstance(parsed, dict) else parsed
        return nodes if isinstance(nodes, list) else []

    # ------------------------------------------------------------ message store

    def section(self, prefix: str, depth: int, reason: bool = False) -> list[dict[str, Any]]:
        """Returns current values (no history) of all topics below prefix up to depth levels.

        @param prefix Topic prefix without trailing slash; "" means the whole tree.
        @param depth Levels below prefix; 0 returns only the prefix node itself.
        @param reason Include the reason chain of the current value.
        """
        clean = prefix.strip().strip("/")
        level = FULL_TREE_DEPTH if clean == "" else max(0, min(int(depth), FULL_TREE_DEPTH))
        return self._store_query(clean, level, history=False, reason=reason)

    def all_topics(self, refresh: bool = False) -> list[dict[str, Any]]:
        """Returns every node of the tree with current value and time (cached briefly)."""
        if not refresh:
            cached = self._cache.get("all_topics", TOPIC_LIST_CACHE_SECONDS)
            if cached is not None:
                return cached
        nodes = self.section("", FULL_TREE_DEPTH, reason=False)
        nodes.sort(key=lambda node: node.get("topic", ""))
        self._cache.put("all_topics", nodes)
        return nodes

    def topic_with_history(self, topic: str, refresh: bool = False) -> dict[str, Any] | None:
        """Returns one topic node including current value, reason and full history.

        Always a single-topic request (levelAmount 0) to keep msgstore memory bounded.
        """
        key = ("history", topic)
        if not refresh:
            cached = self._cache.get(key, HISTORY_CACHE_SECONDS)
            if cached is not None:
                return cached
        nodes = self._store_query(topic, 0, history=True, reason=True)
        node = next((candidate for candidate in nodes if candidate.get("topic") == topic), None)
        self._cache.put(key, node)
        time.sleep(HISTORY_REQUEST_PAUSE_SECONDS)
        return node

    def current(self, topic: str) -> dict[str, Any] | None:
        """Returns the current node (value, time, reason) of exactly one topic, uncached."""
        nodes = self._store_query(topic, 0, history=False, reason=True)
        return next((candidate for candidate in nodes if candidate.get("topic") == topic), None)

    # ---------------------------------------------------------------- publish

    def publish(self, topic: str, value: Any, qos: int, retain: bool, reason_message: str, timestamp: str) -> int:
        """Publishes one message via POST /publish. Returns HTTP status (200 or 204 on success)."""
        body = {
            "topic": topic,
            "value": value,
            "reason": [{"message": reason_message, "timestamp": timestamp}],
            "qos": qos,
            "retain": retain,
        }
        status, payload = self._request("POST", "/publish", body)
        if status not in (200, 204):
            raise YahaError(f"{self.name}: publish failed with HTTP {status}: {payload[:300].decode('utf-8', 'replace')}")
        self._cache.drop("all_topics")
        return status

    # -------------------------------------------------------------- filestore

    def config_get(self, key: str, refresh: bool = False) -> Any:
        """Reads one filestore key (e.g. /automation/rules) and returns the parsed JSON value."""
        clean = "/" + key.strip().strip("/")
        cache_key = ("config", clean)
        if not refresh:
            cached = self._cache.get(cache_key, CONFIG_CACHE_SECONDS)
            if cached is not None:
                return cached
        status, payload = self._request("GET", "/kvstore" + urllib.parse.quote(clean, safe="/"))
        if status == 404:
            raise YahaError(f"{self.name}: config key {clean} not found")
        if status != 200:
            raise YahaError(f"{self.name}: config key {clean} returned HTTP {status}: {payload[:300]!r}")
        text = payload.decode("utf-8", "replace")
        try:
            value = json.loads(text)
        except json.JSONDecodeError:
            value = text
        self._cache.put(cache_key, value)
        return value
