# YAHA MCP server — usage guide

The `yaha` MCP server gives Claude (or any MCP client) the same access to the YAHA home automation that the
browser GUI has: the complete message history, current values, configuration, and publishing messages.
It talks only to the HTTP browser interface (nginx on the Raspberry Pi), never to MQTT directly and never
via SSH.

It is meant for any kind of question or task about the house, for example "When did we get home?",
"How long was the floodlight on yesterday?", "Why did the dressing room light switch on?",
"Which windows are open?", "What was the coldest outdoor temperature this week?", or
"Switch the ventilation off" (with approval).

## 1. Setup

Requirements: Python 3.9+ standard library only (no packages to install), network access to the Pi.

Register once for all Claude Code sessions (user scope):

```sh
claude mcp add --scope user yaha -- python3 /Users/volkerbohm/dev/yaha/mqtt/tools/yaha_mcp/yaha_mcp_server.py
```

Check it: `claude mcp list` should show `yaha: ... ✔ Connected`. New sessions then have the tools
`mcp__yaha__yaha_*`. Remove with `claude mcp remove --scope user yaha`.

Optional environment variables (pass with `-e KEY=value` to `claude mcp add`):

| Variable | Default | Meaning |
|----------|---------|---------|
| `YAHA_SYSTEMS` | `{"yahapi":"http://192.168.0.4","yaha2":"http://192.168.0.183"}` | JSON map system name → base URL |
| `YAHA_DEFAULT_SYSTEM` | `yahapi` | System used when a tool call has no `system` argument |
| `YAHA_TZ` | `Europe/Berlin` | Timezone for input interpretation and output |
| `YAHA_HTTP_TIMEOUT` | `20` | Seconds per HTTP request |
| `YAHA_CONFIG_KEYS` | (empty) | Extra comma separated filestore keys for `yaha_config_list` |

### Systems

| Name | Address | Role |
|------|---------|------|
| `yahapi` (default) | 192.168.0.4 | Production. Real history of the house. Publishing works. |
| `yaha2` | 192.168.0.183 | Unfinished backup. Has its own message store; no HTTP-MQTT service runs there, so `yaha_publish` fails. |

## 2. Data model you need to know

**Topics** are slash separated paths: `<floor>/<room>/<device>/<sensor or switch>/<property>`.
Roots on `yahapi`: `ground`, `first`, `cellar`, `outdoor` (devices), `status` (derived house state such as
`status/presence`, `status/alert/*`, `status/motion/*/latest`), `system` (service events, presence mirror),
`$SYS` (broker statistics), `$MONITOR` (service monitoring, ~1300 topics, excluded from searches by default).
Names are English and contain spaces, e.g. `ground/hallway/wall/motion sensor/detection state`.

**Commands vs. state**: a command is published to `<device topic>/set`; the device reports its state on
`<device topic>`. Example: `ground/livingroom/zwave/switch/floodlight/set` → `.../floodlight`.

**Message** = `{topic, value, time, reason}`. Values are strings or numbers (`"on"`, `"off"`, `0`/`1` for
motion, `"12.60"` for temperatures, enumerations like `awake`/`absent`/`sleeping`).

**Reason chain** explains *why* a message exists, oldest origin first. Typical entries:
- `received from zwave network node: <n>` / `received from arduino` / `send by ESP8266` — device reports
- `Events: allOf: <topic> (<time>), ...` — which sensor events triggered an automation rule
- `Rule: <path>, check: ... value: ...` — the automation rule that set the value. `<path>` (e.g.
  `motion/rules/homecoming`) is the JSON path of the rule inside filestore key `/automation/rules`
- `request by browser` / `Request by User` — a person used the GUI
- `request by Claude (yaha MCP): <note>` — published through this server

**History** is kept per topic by the message store and is bounded (roughly 10 000–12 000 entries per topic;
fast changing topics reach back days, slow ones months). Every history tool reports
`coverage` / `coverage_warnings` when the stored history starts later than the requested window. Never
claim "nothing happened" for a period that is not covered.

**Times**: input and output are local time (`Europe/Berlin`), output format
`2026-10-17 12:01:59 +02:00 Sat`. The store itself works in UTC.

**Configuration** lives in the filestore (key/value JSON):
`/automation/rules` (all rules, tree of `<area>/rules/<name>`), `/zwave/settings`,
`/yaha/gui/config/Computer` and `/Tablet` (GUI metadata per topic: `topicType`, `valueType`, `enumList`),
`/remoteservice/services`, `/valueservice/values`.

## 3. Tools

All tools accept an optional `system` (`yahapi` default, `yaha2`). Results are JSON text.

### `yaha_overview`
Start here. Root areas with topic counts, biggest sub-areas, latest update per root, timezone, current time.

### `yaha_find_topics`
Discover topic names. `query`: words that must all occur (case-insensitive), or a regex with `regex: true`.
Words match as substrings (`temp` finds `temperature`); topics where the words start a word are listed first
(`door` ranks `front door` before `outdoor`). `prefix` restricts the area; `include_monitor` adds `$MONITOR/`
topics; `limit` (default 100).
Returns topic, current value, time and GUI metadata (e.g. `enumList` shows the allowed values).

```json
{"query": "motion sensor detection"}
{"query": "window", "prefix": "cellar"}
{"query": "temperature$", "regex": true}
```

### `yaha_get`
Fresh current value, time and reason chain. Either `topics` (exact names) or `prefix` + `depth`
(levels below prefix, `0` = prefix node only). `include_reason` (default true).

```json
{"topics": ["status/presence", "status/alert/window"]}
{"prefix": "status/alert", "depth": 2, "include_reason": false}
```

### `yaha_history`
Timeline of stored messages, merged across topics.

| Argument | Meaning |
|----------|---------|
| `topics` / `prefix` / `query` (+`regex`, `include_monitor`) | topic selection, combinable |
| `max_topics` | safety cap, default 60, hard limit 300 — narrow the selection instead of raising it |
| `since`, `until` | window. `-24h` default. Formats: `-2h`, `-7d`, `today`, `yesterday`, `2026-10-16`, `2026-10-16 18:30`, ISO with offset |
| `value_equals`, `value_regex` | keep only matching values (string compare, case-insensitive) |
| `reason_contains` | keep events whose reason chain contains text (e.g. a rule name) |
| `changes_only` | drop events that repeat the previous value of the same topic |
| `order` | `desc` (default, newest first) or `asc` |
| `limit` | max events returned, default 200, hard limit 2000; `events_matched` tells the full count |
| `include_reason` | default true; set false for compact sensor timelines |

```json
{"topics": ["status/presence"], "since": "-7d", "changes_only": true}
{"prefix": "ground/hallway", "query": "motion", "since": "today", "value_equals": "1", "include_reason": false}
{"query": "presence", "reason_contains": "homecoming", "since": "-30d"}
```

### `yaha_history_summary`
Per-topic statistics for a window (same selection and window arguments as `yaha_history`):
events in window, first/last event, value at window start; numeric topics get min/max/avg with times;
other topics get value counts, number of changes and `time_in_value` (time spent in each value,
e.g. how long a switch was `on`).

```json
{"query": "outdoor temperature", "since": "-7d"}
{"topics": ["ground/livingroom/zwave/switch/floodlight"], "since": "yesterday", "until": "today"}
```

### `yaha_publish` — changes the real house
Publishes one message like the GUI does (qos 1, not retained by default). Arguments: `topic`, `value`,
`note` (added to the reason chain), `qos`, `retain`, `verify_topic` (default: topic without `/set`),
`verify_seconds` (default 5, `0` = do not wait). Returns the state before and after and whether the state
topic updated.

**Rule: only call after the user explicitly approved this exact topic and value.** State what will change
and how to undo it (usually publishing the previous value). Read the current state with `yaha_get` first.

```json
{"topic": "ground/wardrobe/zwave/switch/ventilation/set", "value": "off", "note": "user asked to stop ventilation"}
```

### `yaha_config_list`
Known filestore keys with size and top-level keys (also discovers keys from `$MONITOR/FileStore/changed`).

### `yaha_config_get`
Read one key. `path` drills into the JSON (slash separated), `search` returns the objects that contain a
word/topic (all words must match, or `regex`), `max_chars` limits output (default 20 000).

```json
{"key": "/automation/rules", "path": "motion/rules/homecoming"}
{"key": "/automation/rules", "search": "status/presence"}
```

## 4. How to answer questions — general method

1. **Map words to topics.** Translate the user's terms to English topic words (Haustür → door, Flur → hallway,
   Bewegung → motion, Licht → light, Wohnzimmer → livingroom, Anwesenheit → presence). Use
   `yaha_find_topics`; check `status/*` first for house-level questions (presence, alerts, cleaning, latest
   motion). Look at GUI metadata (`enumList`) to learn the value vocabulary.
2. **Read the timeline** with `yaha_history` (use `changes_only` for state topics, `value_equals` to find
   specific transitions) or numbers with `yaha_history_summary`.
3. **Explain with reasons.** The reason chain says which sensor, rule or person caused a value. Look up the
   rule with `yaha_config_get` (`path` = rule path from the reason) to understand its logic and caveats.
4. **Cross-check** with the raw sensors named in the reason (`Events: ...`) when the derived state could be
   wrong, and respect `coverage_warnings`.
5. **Answer in local time** with weekday, say how certain the evidence is (derived state vs. raw sensor),
   and name the topics used.

Example — "When did we get home?": find `status/presence` (values `awake`/`absent`/`sleeping`), read its
history with `changes_only`, look for transitions `absent → awake`; the reason names the rule
(`motion/rules/homecoming`) and the hallway motion sensors that triggered it.

## 5. Safety and load limits (important)

- **History is always fetched one topic per request.** A message-store query for a whole subtree *with
  history* makes msgstore materialise the full decompressed history in RAM. On 2026-10-17 a root query with
  history drove msgstore on `yahapi` to ~800 MB and the kernel OOM killer terminated it (about one minute of
  messages lost). The server therefore never sends `history: true` with `levelAmount > 0`. Do not work
  around this with curl.
- Per-topic history responses are at most ~2 MB and ~0.5 s. A selection of 60 topics takes ~5–15 s.
- Topic list (240 KB) is cached 30 s, per-topic history 20 s, configuration 5 min.
- `yaha_publish` is the only write operation. The server cannot change configuration.
- Production rules from `.claude/commands/product-problem.md` apply: read-only first, every change needs a
  user yes, never change fixed addresses or ports.

## 6. Troubleshooting

| Symptom | Cause / action |
|---------|----------------|
| `connection refused` / `HTTP 502` from `/store` | msgstore down or restarting. `ssh pi@yahapi systemctl status msgstore` |
| `no topic matches the selection` | wrong words. Use `yaha_find_topics` with fewer words, or regex |
| `N topics selected, max_topics is 60` | selection too broad. Add `query` words or a narrower `prefix` |
| `publish failed with HTTP 500` | httpmqtt could not forward to the broker; check `systemctl status httpmqtt broker` |
| empty `events` but topic exists | window outside stored history (see `coverage_warnings`) or filters too strict |
| `yaha` missing in `claude mcp list` | run the registration command in section 1, start a new session |

## 7. Development

Code: `tools/yaha_mcp/` (see `SPEC.md`). Tests (mock HTTP server, no real system):

```sh
python3 -m unittest discover -s tools/yaha_mcp/test -v
```

Manual protocol check:

```sh
printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"yaha_overview","arguments":{}}}' \
  | python3 tools/yaha_mcp/yaha_mcp_server.py
```
