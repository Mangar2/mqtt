# SerialDevice Oracle Data

This directory contains the phase-0 oracle baseline for the SerialDevice reconstruction.

## Contents

- `schema/`: JSON schemas for oracle suite/case files.
- `generator/`: deterministic legacy-harness generator.
- `data/`: generated oracle datasets used by C++ parity tests.

## Generate oracle data

Run from repository root:

```sh
node test/oracle/serialdevice/generator/generate_oracle_serialdevice.js \
  --config test/oracle/serialdevice/generator/config/default-oracle-config.json \
  --out test/oracle/serialdevice/data \
  --families A,B,C,D,E,F \
  --verify-determinism \
  --update-manifest
```

## Diff gate mode

To prove refactor-only changes did not alter baseline outputs:

```sh
node test/oracle/serialdevice/generator/generate_oracle_serialdevice.js \
  --config test/oracle/serialdevice/generator/config/default-oracle-config.json \
  --out test/oracle/serialdevice/data \
  --families A,B,C,D,E,F \
  --verify-determinism \
  --update-manifest \
  --fail-on-diff
```

Exit codes:
- `0` success
- `2` schema/format failure
- `3` non-deterministic generation
- `4` output diff detected with `--fail-on-diff`
- `10` generator execution failure
