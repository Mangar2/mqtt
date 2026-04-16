# TEST_SPEC.md — topic/trie/test

Unit tests for Module 3.2: Subscription Trie.

## Tag

`[subscription_trie]`

## Test cases

### insert

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `insert_single` | One subscription inserted | client="A", filter="sport/tennis" | `size() == 1` |
| `insert_two_clients_same_filter` | Two clients, same filter | A→"a/b", B→"a/b" | `size() == 2` |
| `insert_same_client_replaces` | Same client + filter inserted twice | A→"a/b" QoS0, then A→"a/b" QoS1 | `size() == 1` |
| `insert_same_client_different_filters` | Same client, two distinct filters | A→"a/b", A→"a/c" | `size() == 2` |
| `insert_wildcard_plus` | '+' level stored correctly | A→"sport/+/player" | `size() == 1` |
| `insert_wildcard_hash` | '#' level stored correctly | A→"sport/#" | `size() == 1` |
| `insert_hash_only` | '#'-only filter | A→"#" | `size() == 1` |

### remove

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `remove_existing` | Insert then remove the same sub | insert A→"a/b", remove A/"a/b" | `size() == 0` |
| `remove_nonexistent_filter` | Remove a filter that was never inserted | remove A/"x/y" | no crash, `size() == 0` |
| `remove_wrong_client` | Insert for A, remove for B | insert A→"a/b", remove B/"a/b" | `size() == 1` |
| `remove_one_of_two_clients` | Two clients, remove one | insert A→"a/b", B→"a/b", remove A/"a/b" | `size() == 1` |
| `remove_prunes_nodes` | After remove, internal nodes cleaned up | insert A→"deep/path/here", remove A/"deep/path/here" | `size() == 0` |
| `remove_wildcard_plus` | Remove '+' subscription | insert A→"sport/+", remove A/"sport/+" | `size() == 0` |
| `remove_wildcard_hash` | Remove '#' subscription | insert A→"sport/#", remove A/"sport/#" | `size() == 0` |
| `remove_empty_trie` | Remove on empty trie | remove A/"a/b" | no crash, `size() == 0` |

### remove_all

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `remove_all_single_client` | Remove all subs of client A | insert A→"a/b", A→"c/d", remove_all(A) | `size() == 0` |
| `remove_all_leaves_other_clients` | A and B have subs, remove A | insert A→"a/b", A→"c/d", B→"e/f", remove_all(A) | `size() == 1` |
| `remove_all_nonexistent_client` | Client never subscribed | remove_all("unknown") | no crash, `size() == 0` |
| `remove_all_empty_trie` | Called on empty trie | remove_all("A") | no crash, `size() == 0` |
| `remove_all_with_wildcards` | Client has wildcard subs | insert A→"#", A→"sport/+", B→"a/b", remove_all(A) | `size() == 1` |
| `remove_all_prunes_nodes` | All nodes must be pruned after full removal | insert A→"x/y/z", remove_all(A) | `size() == 0` |
