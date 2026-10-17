# TEST_SPEC — yaha_mcp

Run: `python3 -m unittest discover -s tools/yaha_mcp/test -v` (from the mqtt repo root).
Tests never contact a real YAHA system. Network tests use a local in-process mock HTTP server.

## TimeUtilTest

| Test | Scenario | Input | Expected |
|------|----------|-------|----------|
| test_relative_offsets | relative past offsets | "-2h", "30m", "1w", "3h ago" | reference minus offset |
| test_keywords_local_midnight | day keywords | "today", "yesterday", "gestern" | local midnight Europe/Berlin as UTC |
| test_date_and_naive_local | date and naive datetime | "2026-10-16", "2026-10-16 18:30" | interpreted as local time |
| test_iso_with_offset | aware ISO | "2026-10-16T18:30:00Z" | same instant |
| test_invalid_raises | garbage | "next tuesday" | ValueError |
| test_format_local | output format | 2026-10-17T10:01:59Z | "2026-10-17 12:01:59 +02:00 Sat" |
| test_format_duration | durations | 45, 3720, 93784 | "45s", "1h 2m", "1d 2h 3m" |

## HistoryTest

| Test | Scenario | Input | Expected |
|------|----------|-------|----------|
| test_select_topics_prefix_query_monitor | selection rules | prefix, query words, regex, $MONITOR topics | prefix boundary respected, all words required, $MONITOR excluded unless allowed |
| test_word_start_score | ranking helper | 'door' vs 'front door'/'outdoor', two terms, None | 1, 0, 2, 0 |
| test_node_events_sorted_with_current | node to events | node with current + 2 history entries | 3 events oldest first, reasons extracted |
| test_only_changes | repeated values | on,on,off,off,on | on,off,on |
| test_event_filter | all criteria | window, value_equals, value_regex, reason_contains | only matching events |
| test_coverage_note | history shorter than window | oldest after since | note returned; none otherwise |
| test_time_in_values | step durations | value changes inside window, state before window | seconds per value incl. state at window start |
| test_summarize_numeric_and_enum | summary modes | numeric series, enum series | numeric min/max/avg; enum counts, changes, time_in_value |

## ServerProtocolTest

| Test | Scenario | Input | Expected |
|------|----------|-------|----------|
| test_initialize_and_list | handshake | initialize with known version, tools/list | echoes version, 8 tools without handler key |
| test_unknown_method_and_notification | protocol edge | unknown method with id, notification | -32601 error, None for notification |
| test_tool_error_is_reported | handler raises | yaha_history without selection | isError true with message |

## MockSystemTest (local mock HTTP server)

| Test | Scenario | Input | Expected |
|------|----------|-------|----------|
| test_section_never_requests_history | tree listing | all_topics() | POST /store with history false, levelAmount 30 |
| test_topic_history_single_topic_requests | history fetch | yaha_history over prefix | one /store request per topic with levelAmount 0 |
| test_history_tool_filters_and_orders | history tool | value_equals, changes_only, order asc | filtered events oldest first, coverage warning |
| test_topic_cap_guard | too many topics | max_topics smaller than selection | ValueError naming the count |
| test_publish_and_verify | publish tool | topic x/set value on | POST /publish body with reason, state_updated true |
| test_publish_http_error | publish fails | mock returns 500 | YahaError |
| test_config_get_path_search_and_missing | filestore | path, search, unknown key | sub-object, matching object paths, error for 404 |
| test_config_list_discovers_monitor_keys | key discovery | FileStore/changed history with keyPath | discovered key listed |
| test_get_and_find_topics | current values and search | yaha_get topics/prefix, yaha_find_topics query, ranking 'door' | values, missing_topics, gui metadata merged, word-start match before substring match |
