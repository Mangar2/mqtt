# MessageTree

## Purpose

A container that stores the most recent value and a compressed history of every MQTT message topic seen by the system. Topics are the natural organising key: the tree mirrors the hierarchical structure of topic paths so that related topics (e.g. all sensors in one room) can be retrieved together efficiently.

## Role in the system

The MessageTree is an internal data structure used by the MessageStore. It is not exposed directly to MQTT or HTTP clients; those clients talk to the MessageStore, which delegates storage and retrieval to the MessageTree. The tree can also be persisted to disk and restored on startup.

## Data model

### Tree structure

The tree is keyed by topic path segments. Each segment of a topic (split on `/`) is one level of the tree. Intermediate nodes that have no data of their own act as routing nodes only. Nodes at any level may simultaneously have data and children.

### Data node

A node that has received at least one message contains:

| Field   | Type            | Meaning                                              |
|---------|-----------------|------------------------------------------------------|
| topic   | string          | Full topic path of this node                         |
| value   | any             | Most recent payload value                            |
| time    | timestamp (ms)  | Wall-clock time of the most recent update            |
| reason  | Reason[]        | List of reason objects attached to the last message  |
| history | HistoryEntry[]  | Compressed record of earlier values (see below)      |

### History

Each time a node is updated, its previous `{time, value, reason}` is appended to a history list. The history list is kept bounded: once its length exceeds a configured maximum, the oldest entries are removed in a batch (hysteresis) to avoid constant trimming. History entries are stored in a compressed form; they are decompressed on retrieval.

### History entry

| Field  | Type             | Meaning                                            |
|--------|------------------|----------------------------------------------------|
| time   | timestamp (ms)   | Time of the entry                                  |
| value  | any              | Value at that time                                 |
| reason | Reason[]         | Reason at that time                                |

### Reason

A reason describes why a message was sent. It is a list of objects, each with at least a `message` string and a `timestamp`. The exact schema is determined by the publishing component.

## Behavior

### Adding a message

When a new message arrives for a topic, the tree locates or creates the node for that topic path. The current content of the node is moved into the history before the new value is stored. The timestamp is set to the current wall-clock time.

### Retrieving a section

A caller can request all nodes under a given topic prefix, up to a configurable depth. The result is a flat array of data-node payloads. History and reason fields are included or excluded based on query parameters.

### Retrieving specific nodes with change detection

A caller can provide a list of `{topic, value, reason}` entries. The tree returns only those nodes whose current state differs from the provided snapshot (value changed or reason changed). This supports efficient polling: a client sends what it already knows and receives only the delta.

### Cleanup

A cleanup operation removes nodes (and their subtrees) that have not been updated within a given number of days. A node is kept if it or any of its descendants has a recent update. After cleanup the tree is compacted; if the root has no remaining children it is reset to an empty tree.

## Persistence

The entire tree can be serialised to a file and read back on startup. The format is an opaque serialised representation of the internal tree object. On load, if the file exists and is valid, the in-memory tree is replaced with the loaded data. If the file is absent or corrupt, the tree starts empty.

## Configuration

| Parameter                      | Type    | Default | Meaning                                                              |
|-------------------------------|---------|---------|----------------------------------------------------------------------|
| maxHistoryLength               | integer | 50      | Maximum number of history entries per node before trimming           |
| historyHysterese               | integer | 10      | Number of entries removed when trimming is triggered                 |
| maxValuesPerHistoryEntry       | integer | 256     | Maximum number of distinct values in one compressed history entry    |
| lengthForFurtherCompression    | integer | 10      | History entry count threshold to trigger additional compression      |
| upperBoundFactor               | number  | 1.2     | Factor applied to determine the upper time bound of an interval      |
| upperBoundAddInMilliseconds    | integer | 1000    | Milliseconds added to the upper bound of an interval                 |
| lowerBoundFactor               | number  | 0.8     | Factor applied to determine the lower time bound of an interval      |
| lowerBoundSubInMilliseconds    | integer | 1000    | Milliseconds subtracted from the lower bound of an interval          |

## Error handling

- If persistence data cannot be loaded (file missing, parse error) the tree starts empty and continues normally.
- If a topic path is malformed (e.g. empty segments) behavior is implementation-defined; the spec requires no crash.

## Architectural notes

- The tree is keyed by topic path segments (split on `/`), not by the full topic string. This is important because retrieval by prefix and depth would be O(n) over all stored topics with a flat map but is O(subtree size) with the hierarchical structure.
- History compression is an important design constraint: history entries are stored in a run-length / interval compressed form so that large numbers of small value changes (e.g. a sensor repeating the same temperature) do not consume unbounded memory.

## Open questions

- Should the tree support concurrent reads during a write, or is single-threaded access assumed (locked by the caller)?
- Should cleanup be topic-pattern-scoped (only clean under a given prefix) or always global?
- Is the history compression algorithm required to be byte-for-byte compatible with the JS implementation (for migration), or can it be redesigned?
