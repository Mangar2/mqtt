# TEST_SPEC.md — store (Module 4)

All tests are tagged `[store]`.

---

## subscription_store_test.cpp — SubscriptionStore (4.1)

| Test case | Section | Scenario | Input | Expected |
|-----------|---------|----------|-------|----------|
| `store_insert_and_size` | insert new sub | Insert one subscription | client_id="c1", filter="a/b" | size() == 1 |
| `store_overwrite_same_filter` | overwrite | Insert same filter twice | client_id="c1", filter="a/b", two different QoS | size() == 1 (overwritten) |
| `store_two_clients_same_filter` | multi-client | Two clients, same filter | c1 and c2, filter="a/b" | size() == 2 |
| `remove_existing_subscription` | remove | Remove known subscription | c1 / "a/b" | size() == 0 |
| `remove_nonexistent_is_noop` | remove noop | Remove filter that was never added | c1 / "x/y" | no exception, size unchanged |
| `subscribers_for_exact_match` | match | Publish to subscribed topic | sub "a/b", publish "a/b" | 1 result with correct client_id |
| `subscribers_for_wildcard_plus` | wildcard + | Publish matches + filter | sub "a/+/c", publish "a/b/c" | 1 result |
| `subscribers_for_wildcard_hash` | wildcard # | Publish matches # filter | sub "a/#", publish "a/b/c/d" | 1 result |
| `subscribers_for_no_match` | no match | Publish to unsubscribed topic | sub "x/y", publish "a/b" | empty |
| `remove_session_clears_all` | remove_session | Remove all subs for a client | c1 has 2 subs | size() == 0 |
| `remove_session_noop_unknown` | remove_session noop | Remove subs for unknown client | unknown client_id | no exception |

---

## retained_message_store_test.cpp — RetainedMessageStore (4.2)

| Test case | Section | Scenario | Input | Expected |
|-----------|---------|----------|-------|----------|
| `store_creates_entry` | store | Store non-empty message | topic="a/b", payload="hello" | size() == 1 |
| `store_overwrites_existing` | overwrite | Same topic stored twice | topic="a/b" with different payloads | size() == 1, latest payload stored |
| `store_empty_payload_deletes` | auto-delete | Store message with empty payload | topic="a/b", payload="" | size() == 0 |
| `store_empty_payload_noop_if_absent` | auto-delete noop | Empty payload when no entry | topic="x/y", payload="" | no exception, size() == 0 |
| `find_exact_match` | find exact | Exact filter matches stored topic | stored "a/b", filter "a/b" | 1 result |
| `find_plus_wildcard` | find + | + filter matches stored topic | stored "a/b", filter "a/+" | 1 result |
| `find_hash_wildcard` | find # | # filter matches stored topics | stored "a/b", "a/b/c", filter "a/#" | 2 results |
| `find_no_match` | find none | Filter with no matching topics | stored "x/y", filter "a/b" | empty |
| `find_system_topic_excluded_from_wildcard` | find system | $SYS topic excluded from wildcard | stored "$SYS/b", filter "+/b" | empty |
| `find_system_topic_exact` | find system exact | Exact filter still matches $SYS | stored "$SYS/b", filter "$SYS/b" | 1 result |
| `find_multiple_matches` | find multi | Multiple stored topics match filter | stored "a/1","a/2", filter "a/+" | 2 results |
| `all_returns_all_messages` | all | Enumerate all messages including system topics | stored "a/b" and "$SYS/x" | all() returns 2 results |
| `all_empty_when_no_messages` | all empty | No messages stored | empty store | all() returns empty vector |

---

## session_store_test.cpp — SessionStore (4.3)

| Test case | Section | Scenario | Input | Expected |
|-----------|---------|----------|-------|----------|
| `create_new_session` | create | Create fresh session | client_id="c1" | size() == 1, load returns value |
| `create_duplicate_throws` | create error | Create session that already exists | client_id="c1" twice | StoreException(SessionAlreadyExists) |
| `load_existing_session` | load | Load stored session | client_id="c1" | returned optional has value, fields match |
| `load_unknown_returns_empty` | load miss | Load non-existent client | client_id="unknown" | nullopt |
| `remove_existing_session` | remove | Remove known session | client_id="c1" | size() == 0 |
| `session_remove_unknown_is_noop` | remove noop | Remove unknown client_id | never-added client | no exception |
| `contains_returns_true_when_present` | contains | Check known client_id | client_id="c1" | true |
| `contains_returns_false_when_absent` | contains miss | Check unknown client_id | client_id="unknown" | false |
| `expired_sessions_immediate_expiry` | expiry=0 | session_expiry_interval==0 | mark_disconnected in past | session in result |
| `expired_sessions_interval_elapsed` | expiry elapsed | Interval has passed | expiry=60s, disconnect 90s ago | session in result |
| `expired_sessions_interval_not_elapsed` | expiry pending | Interval not yet passed | expiry=60s, disconnect 30s ago | not in result |
| `expired_sessions_never_expires` | expiry never | session_expiry_interval==0xFFFFFFFF | any disconnect time | not in result |
| `expired_sessions_no_disconnect_time` | no disconnect | No mark_disconnected call | connected session | not in result |
| `remove_also_clears_disconnect_time` | remove clears | Remove then re-create | c1 removed and recreated | not expired |
| `all_returns_all_sessions` | all | Enumerate all stored sessions | three sessions created | all() returns vector of size 3 |
| `all_empty_when_no_sessions` | all empty | No sessions | empty store | all() returns empty vector |

---

## inflight_store_test.cpp — InflightStore (4.4)

| Test case | Section | Scenario | Input | Expected |
|-----------|---------|----------|-------|----------|
| `create_entry_and_size` | create | Add one entry | client_id="c1", packet_id=1, dir=Outbound | size_for("c1") == 1 |
| `create_multiple_entries` | multi create | Add two entries for same session | pkt 1 and pkt 2 | size_for("c1") == 2 |
| `entries_for_returns_all` | entries_for | Retrieve entries | 2 entries stored | vector of size 2 |
| `entries_for_unknown_client` | entries_for empty | Query unknown client | never-added client | empty vector |
| `update_changes_state` | update | Advance handshake state | create entry, call update | entry state updated |
| `update_unknown_throws` | update error | Update non-existent entry | packet_id not in store | StoreException(PacketIdNotFound) |
| `remove_entry_decrements_size` | remove | Remove known entry | create then remove | size_for("c1") == 0 |
| `inflight_remove_unknown_is_noop` | remove noop | Remove entry not present | unknown packet_id | no exception |
| `remove_last_entry_removes_session_bucket` | remove last | Remove only entry | create one, remove it | entries_for returns empty |
| `is_packet_id_in_use_true` | registry true | Entry exists | packet_id=1 present | true |
| `is_packet_id_in_use_false` | registry false | Entry absent | packet_id=99 not stored | false |
| `is_packet_id_in_use_direction_mismatch` | registry dir | Same packet_id, different direction | Outbound stored, query Inbound | false |
| `entries_for_does_not_include_other_clients` | isolation | Two clients | c1 and c2 entries | entries_for("c1") has only c1's entries |
