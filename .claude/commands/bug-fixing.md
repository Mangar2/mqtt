# bug-fixing skill

Caveman rules. AI only. No deviation. No skipping steps.

## hard rules apply to all steps

- never analyze outside scope defined in bug file
- never assume facts user did not state
- never expand search to packet types, QoS levels, modules, code paths not named in bug file
- if temptation arises to look at related code "just to check" -> stop -> ask user
- user time is money -> any deviation wastes money -> forbidden
- before any code search or file read in steps 4+ : reread bug file scope section -> if read target outside scope -> stop
- ORIGINAL BUG IS THE ONLY GOAL. never replace original bug with a sub-problem found during analysis
- a "suspect" is not a "cause". suspect becomes cause only when trace evidence proves it 100%
- proof must always target the ORIGINAL reported symptom, not a derived intermediate observation
  - example wrong: report = "cpu at 100%". observation = "thread X loops doing Y". proof of "thread X loops doing Y" is NOT proof that Y causes the 100% cpu. missing link: cpu measurement showing X drops to 0% when Y is suppressed, or cpu profiling attributing the load to Y
  - rule: every chain link from suspect to original symptom must be backed by evidence. no link skipped no link assumed
- forbidden: fixing a suspected sub-issue without proof it causes the reported symptom
- forbidden: drifting goal from "fix reported bug X" to "fix interesting thing Y discovered along the way"
- after every action reread `## user report` -> ask: does this action move us closer to fixing exactly that report? if no -> stop

## step 1 capture bug

- create directory `spec/bug/<short-slug>/`
- create file `spec/bug/<short-slug>/bug.md`
- all further bug documentation files and test cases for this bug go into this directory
- paste user input verbatim under heading `## user report`
- no edits no rewording no interpretation

## step 2 minimal test case

- goal: have a single automatically executable command that reproduces the reported bug
- check if a test case covering the reported symptom already exists in the standard test suite
  - if yes: document the exact run command under `## test case` in the bug file; run it and record the output
  - if no: create a dedicated test case file inside `spec/bug/<short-slug>/`
    - the test must be runnable with a single command (e.g. `python3 spec/bug/<short-slug>/repro.py` or a ctest invocation)
    - test must produce a clear PASS / FAIL signal
    - test must be as minimal as possible — reproduce only the reported symptom, nothing more
    - document the run command and expected failure output under `## test case` in the bug file
- run the test case and confirm it fails (demonstrates the bug)
- do NOT create a test case that passes — a passing test proves nothing at this stage
- document result under `## test case` in the bug file

## step 3 clarify and lock scope

- read user report
- list every ambiguity every missing fact
- ask user one question per ambiguity
- may run read-only diagnostics to gather facts:
  - `ssh <host> top -H -p <pid>` thread level cpu usage
  - `ssh <host> top -H` find broker pid
  - `ps -eLf | grep mqtt-broker` thread list
  - `cat /proc/<pid>/status` `/proc/<pid>/stack` `/proc/<pid>/task/<tid>/stack`
  - `ss -tnp` socket state
  - tail existing log file if available
- after answers write `## scope` section in bug file
- scope section MUST contain explicit allow list and explicit deny list
- example allow: "QoS0 publish path only"
- example deny: "do not analyze QoS1 QoS2 retransmit inflight retained will"
- scope section MUST contain `## confirmed facts` list
- user must confirm scope section before step 4
- once locked scope is law for rest of skill

## step 4 short analysis only

- HARD LIMIT max 10 minutes wall time max 5 file reads
- read only files inside scope allow list
- if file path unclear if it is in scope -> treat as out of scope -> skip
- write `## hypothesis` section in bug file
- if confidence low or no hard suspect -> write "no hard suspect" -> go step 5
- forbidden: speculative code reads, "let me also check", chained searches, exploring related modules
- forbidden: presenting hypothesis without naming exact file and line

## step 5 trace based analysis

- build invocation line for broker with traces enabled writing to file
- template:
  ```
  python3 test/deploypi.py \
    --remote-host pi@raspberrypi \
    --remote-dir /home/pi/mqtt \
    --trace-level <level> \
    --trace-modules <modules-csv> \
    --trace-output /home/pi/mqtt/bug-<slug>.trace \
    --follow-log
  ```
- pick `--trace-level` and `--trace-modules` strictly from scope allow list
- give user the exact command to run
- give user the exact reproduction steps
- wait for user message containing "weitermachen" (or english "continue")
- on continue: read trace file, analyze only events inside scope
- if root cause clear -> step 7
- else -> step 6

## step 6 add temporary traces

- add `TRACE_GUARD` calls only in code paths inside scope
- each temporary trace MUST have comment marker:
  `// BUG-TRACE-TEMP <bug-slug> remove after fix`
- if trace is generally useful keep it without marker, document in bug file under `## kept traces`
- rebuild deploy
- back to step 5

## step 6b full-flow trace coverage (if first trace round was insufficient)

- trigger: returned to step 6 a second time because step 5 traces did not give complete picture
- now instrument the ENTIRE relevant flow end to end, not just one suspected spot
- list every step of the flow inside scope (from entry event to symptom occurrence)
- add a `TRACE_GUARD` at each step with consistent correlation fields (fd, client_id, packet_id, job_type, etc.) so the full chain is reconstructable from the log
- if a class on that flow has no access to the tracer -> wire it in
  - add `StructuredTracer *tracer` (or equivalent) as constructor parameter
  - store as member in header
  - propagate from owners through the construction chain
  - this can be a LARGE code change touching many files - that is allowed and expected here
  - keep the constructor wiring permanently even after the temporary traces are removed (so future bugs do not need the same plumbing again)
  - mark only the `TRACE_GUARD` call sites with `// BUG-TRACE-TEMP <bug-slug>`, NOT the constructor parameter or member
- rebuild deploy
- back to step 5

## step 7 fix

- precondition: root cause is PROVEN by trace evidence from step 4. no proof -> back to step 4. never fix on suspicion alone
- implement minimal fix targeting confirmed root cause
- no refactor no cleanup no unrelated changes
- ask user to verify against ORIGINAL bug report symptom (not against the sub-issue you fixed)
- if user confirms original symptom gone -> done
- if user says still broken:
  - HARD RULE: do not attempt second fix on same suspect
  - first fix failed = your "proof" was not real proof = analysis was incomplete
  - go to step 5 with new traces designed to disprove or prove the suspect lockstep with the ORIGINAL symptom
  - traces must capture both: (a) the suspected mechanism (b) the user-reported symptom event, so correlation can be checked
  - only after new traces show 100% causal link between suspect and original symptom may a second fix be tried
  - if no causal link found -> abandon suspect -> reread `## user report` -> step 4 with fresh hypothesis
- after two failed fix attempts on different suspects: stop and ask user for more reproduction details, do not guess further
- on success:
  - grep `BUG-TRACE-TEMP <bug-slug>` -> remove all matches
  - update bug file `## resolution` section: root cause, proof (which trace events), fix summary, files touched
  - integrate test case into standard test suite:
    - if test case was created in `spec/bug/<short-slug>/`: move or copy it to the appropriate location in the standard test suite (integration tests, unit tests, or performance tests)
    - if test case already existed in standard suite: verify it now passes
    - document the final test location under `## resolution` in the bug file
  - close skill
