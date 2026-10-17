# yaha_mcp — MCP server for the YAHA HTTP browser interface

## Purpose

Model Context Protocol server (stdio, JSON-RPC 2.0, newline delimited) that exposes the YAHA message
store history, current values, publish and filestore configuration as tools. Python standard library
only. Usage documentation: `README.md`.

## Files

| File | Responsibility |
|------|----------------|
| `yaha_mcp_server.py` | Entry point. Protocol loop: `initialize`, `ping`, `tools/list`, `tools/call`, notifications ignored |
| `yaha_tools.py` | Tool schemas (`TOOLS`), handlers, argument validation, system selection |
| `yaha_client.py` | `YahaClient`: HTTP access to `/store`, `/publish`, `/kvstore` with TTL cache and size limit |
| `yaha_history.py` | Pure functions: topic selection, `Event` extraction, filters, change detection, summaries |
| `yaha_timeutil.py` | Time expression parsing and local time formatting |
| `test/` | `TEST_SPEC.md` and unittest suite with in-process mock HTTP server |

## Public API

### Tools

| Tool | Behaviour |
|------|-----------|
| `yaha_overview` | full topic list (no history) aggregated per root and second level |
| `yaha_find_topics` | query/regex/prefix match over topic names + GUI metadata merged from `/yaha/gui/config/*` (`topicRank` dropped); plain queries sorted by `word_start_score` (terms at word start first), stable alphabetical otherwise |
| `yaha_get` | current nodes for exact topics (one `levelAmount 0` request each) or prefix section without history |
| `yaha_history` | per-topic history fetch, merge, `EventFilter`, optional `only_changes`, sort, limit, coverage warnings |
| `yaha_history_summary` | `summarize()` per topic |
| `yaha_publish` | POST `/publish` with reason `request by Claude (yaha MCP)[: note]`, then polls state topic until its time changes or timeout |
| `yaha_config_list` | default keys + `YAHA_CONFIG_KEYS` + `keyPath` values seen in `$MONITOR/FileStore/changed` history |
| `yaha_config_get` | GET `/kvstore/<key>`, optional slash `path`, optional `search` returning enclosing object paths |

### `YahaClient`

| Method | Request |
|--------|---------|
| `section(prefix, depth, reason)` | POST `/store` `{topic, levelAmount, history:false, reason, time:true}`; prefix `""` forces depth 30 |
| `all_topics(refresh)` | `section("", 30)` sorted, cached 30 s |
| `topic_with_history(topic, refresh)` | POST `/store` `{topic, levelAmount:0, history:true, reason:true}`, cached 20 s, 20 ms pause after request |
| `current(topic)` | POST `/store` `levelAmount 0` without history, uncached |
| `publish(topic, value, qos, retain, reason_message, timestamp)` | POST `/publish` JSON; 200/204 success else `YahaError` |
| `config_get(key, refresh)` | GET `/kvstore/<key>`, JSON parsed (fallback raw text), cached 300 s; 404 → `YahaError` |

## Behaviour and constraints

- History is requested only for single topics (`levelAmount 0`). A subtree history query caused an OOM
  kill of msgstore on 2026-10-17; `section()` has no history parameter by design.
- Topic selection for history tools is capped by `max_topics` (default 60, hard 300); exceeding it is an
  error listing the count, never a silent truncation. Event output is capped by `limit` (default 200, hard
  2000) with `events_matched` and `truncated`.
- `$MONITOR/` topics are excluded from prefix/query selection unless `include_monitor` or a `$MONITOR`
  prefix is given. Explicit `topics` are always used.
- Events = current value + history, sorted ascending internally. `changes_only` compares canonical value
  text (`1`, `1.0`, `"1"` equal) over the full series before window filtering.
- `coverage_note` reports when the oldest stored event is newer than `since`.
- `summarize` treats a window as numeric when ≥ 80 % of values parse as numbers; otherwise value counts,
  change count and step-function `time_in_value` (state before the window counts from window start).
- Times: input parsed by `parse_user_time` (relative, keywords incl. German `heute/gestern/vorgestern`,
  date, naive local, ISO); output `YYYY-MM-DD HH:MM:SS +HH:MM Day` in `YAHA_TZ`.
- Responses larger than 20 MB are rejected. HTTP timeout `YAHA_HTTP_TIMEOUT` (default 20 s).
- Tool errors (`YahaError`, `ValueError`) are returned as `isError: true` results; unexpected exceptions
  are logged to stderr and returned as internal error results. stdout carries protocol messages only.
- Supported protocol versions: `2025-06-18`, `2025-03-26`, `2024-11-05` (echo when requested, else newest).
