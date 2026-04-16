# trie — Subscription Trie (Module 3.2)

Trie-based storage for MQTT 5.0 topic subscriptions.
Depends on: `data_model/subscription/` (Module 1.6).

## Files

| File | Plan ref | Contents |
|------|----------|----------|
| `subscription_trie.h` | 3.2 | `SubscriptionTrie` class declaration. |
| `subscription_trie.cpp` | 3.2 | `SubscriptionTrie` implementation. |

## Module 3.2 — SubscriptionTrie

### Purpose

Stores MQTT subscriptions in a trie where each node represents one
topic level (separated by `/`). Supports insertion, targeted removal,
and bulk removal of all subscriptions for a given client.

Matching (3.3) will traverse this trie; the trie itself does not perform
any wildcard expansion — it is a pure storage structure.

### Public API

```cpp
namespace mqtt {

class SubscriptionTrie {
public:
    // 3.2.2 Insert or replace a subscription for a client.
    void insert(std::string_view client_id, const Subscription& sub);

    // 3.2.3 Remove a specific subscription for a client.
    void remove(std::string_view client_id, std::string_view topic_filter);

    // 3.2.4 Remove all subscriptions for a client.
    void remove_all(std::string_view client_id);

    // Returns the total number of (client, filter) pairs stored.
    [[nodiscard]] std::size_t size() const noexcept;
};

} // namespace mqtt
```

### Data Structure (3.2.1)

Each trie node holds:
- `children`: map from topic-level string → unique_ptr\<Node\>
- `subscriptions`: map from client_id string → Subscription

The root node is a sentinel; no subscriptions are stored there.
Topic levels are separated by `/`; the level strings `"+"` and `"#"`
are stored as-is (wildcard semantics are handled by the matcher in 3.3).

### Behaviour

#### `insert`

Splits `sub.topic_filter.value` on `/` into topic levels.
Traverses (creating) one node per level from the root.
Stores `sub` keyed by `client_id` at the terminal node.
If the client already has a subscription at that node, it is overwritten.

#### `remove`

Splits `topic_filter` on `/`. Traverses the trie following the exact
level path. At the terminal node erases the entry for `client_id`.
After each recursive return, prunes (deletes) child nodes that are
empty (no subscriptions and no children).

Does nothing if the path or client entry does not exist.

#### `remove_all`

Performs a depth-first traversal of the whole trie. At every node,
erases the entry for `client_id` from `subscriptions`.
After processing all children, prunes children that became empty.

Does nothing if `client_id` has no subscriptions.

#### `size`

Counts the total number of stored (client_id, filter) pairs by
recursively summing `subscriptions.size()` across all nodes.

### Constraints

- Thread safety: none — external synchronisation required for concurrent use.
- No subscriptions are stored at the root node.
- Empty nodes (no subscriptions, no children) must not remain after
  remove/remove_all — pruning is mandatory.
