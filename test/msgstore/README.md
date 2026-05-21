# MessageStore synchronous test mode input

Use this folder for input files passed to:

`yahamsgstoreclient --test <input-file>`

## Line format

Each non-empty and non-comment line (`#...`) must be a JSON object with YAHA envelope shape:

`{"message":{"topic":"...","value":...,"reason":[{"message":"...","timestamp":"..."}]}}`

## Example

See `generated_sample.jsonl` in this directory.

## Generator note

`generate_messages.py` supports `--fixed-value <number>` to generate files where all messages use the same numeric value.
`generate_messages.py` supports `--topic-count <number>` to generate multiple distinct topics.
Generated topics vary at different segment positions and each message selects one random topic from the generated topic pool.

## Testcases

### testcase1

- `testcase1_5000.jsonl`: 5000 messages, constant numeric value (`34.0`), identical reason text in all messages and all reason entries.
- `testcase1_50000.jsonl`: 50000 messages, constant numeric value (`34.0`), identical reason text in all messages and all reason entries.

This dataset is intended for interval-compression validation with one long regular series.

### testcase2

- `testcase2_5000.jsonl`: 5000 messages, varying numeric values, identical reason text in all messages and all reason entries.
- `testcase2_50000.jsonl`: 50000 messages, varying numeric values, identical reason text in all messages and all reason entries.

This dataset is intended for mixed history-bucket behavior under changing value input with constant reason text.

