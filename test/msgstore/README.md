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

## Memory measurement

Use `measure_memory.py` to compare MessageStore reference-run memory with workload-run memory.
The script uses a deterministic handshake in `yahamsgstoreclient --test-handshake`:

1) process announces `ready_for_start`
2) script measures `reference_run.rss_before_load_command_kib` / `workload_run.rss_before_load_command_kib`
3) script sends `load`
4) process announces `ready_for_end`
5) script measures `reference_run.rss_after_load_command_kib` / `workload_run.rss_after_load_command_kib`
6) script sends `exit`

The script runs:

1) reference run with an empty input file
2) workload run with generated scenario or existing input file

and reports explicit keys:

- `reference_run.rss_before_load_command_kib`
- `reference_run.rss_after_load_command_kib`
- `reference_run.sampled_rss_peak_kib`
- `rss_before_load_kib`
- `rss_after_load_kib`
- `sampled_rss_peak_kib`
- `reference_run.exported_file_size_kib`
- `exported_file_size_kib`

### Example: 1000 topics with one message each

```bash
python3 test/msgstore/measure_memory.py --scenario topics_1000_one_message
```

### Example: use existing dataset file

```bash
python3 test/msgstore/measure_memory.py --input-file test/msgstore/testcase3_1000000_1000topics.jsonl
```

### Notes

- Uses `ps` RSS sampling (KiB).
- Sampling interval can be changed with `--sample-interval-ms`.
- Temporary files are removed by default; use `--keep-artifacts` for debugging.

