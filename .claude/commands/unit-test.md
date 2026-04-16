# Unit Test Guidelines

## Framework

Catch2 v3. Test files live under `tests/unit/`.  
One test file per production module (e.g. `test_variable_byte_integer.cpp` for module 2.1.1).

## Running tests

```sh
# Configure + build + run
cmake --preset test
cmake --build --preset test
ctest --preset test --output-on-failure
```

## Coverage

```sh
cmake --preset test-coverage
cmake --build --preset test-coverage
ctest --preset test-coverage

# Generate report (requires llvm-profdata and llvm-cov on PATH)
llvm-profdata merge -sparse build/test-coverage/default.profraw -o coverage.profdata
llvm-cov report build/test-coverage/mqtt-broker-tests \
    -instr-profile=coverage.profdata \
    --ignore-filename-regex="(catch2|_deps)"
```

**Minimum required: 90% line coverage for all production code.**  
A task is not complete until coverage is verified.

## Analysing test failures

When a test fails, establish the root cause before touching any code:

1. Read the full failure output — assertion, file, line, expected vs. actual.
2. Locate the relevant specification (see `spec/implementierungsplan.md` and `spec/anforderungskatalog.md`).
3. Decide: is the **implementation wrong** or is the **test wrong**?
   - If the spec clearly defines the expected behaviour and the implementation deviates → fix the code.
   - If the test asserts something that contradicts the spec or makes a wrong assumption → fix the test.
   - If it is genuinely ambiguous, ask before proceeding.
4. State your conclusion in one sentence before making any change.
5. Fix **either** the code **or** the test — never both in the same step unless they are provably independent.

Never mark a test `[!shouldfail]`, skip it, or comment it out to make the build green.

## Writing tests

- Test one behaviour per `TEST_CASE`.
- Use `SECTION` to group related assertions within a case.
- Name test cases descriptively: `"Variable Byte Integer encodes 0 as single byte 0x00"`.
- Do not test implementation details — test observable behaviour.
