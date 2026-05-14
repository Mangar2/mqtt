One refactoring skill only.
Use this for all modules.

Caveman language.
No format chars.
No unneeded words.

Goal
Keep architecture clean.
Find hard failure gaps.
Fix all non P2.
Prove with tests.
Update spec and todo report.

Flow
1 analyze module code and tests
2 write todo report in spec/todo-<module>-error-handling-report.md
3 implement all non P2 fixes
4 update module SPEC and TEST_SPEC
5 run diagnostics
6 run client coverage command

Execution gate strict order
Never start implementation before step 1 and step 2 done.
If todo report not updated stop work and return to analysis.
First output of each run is report delta with todo checkboxes.
Only after report delta exists implementation may start.

Architecture law strict
Fachclient has only fachlogik.
Generic client handles all broker communication aspects.
Fachclient offers only virtual interface to generic client.
Any other communication path is strictly forbidden.

Architecture law broker ownership
Generic client owns connect disconnect reconnect keepalive ping subscribe unsubscribe publish ack handling state handling error mapping.
Fachclient must not duplicate any of these.

Architecture law forbidden patterns
No broker transport callbacks in fachclient for connect disconnect publish subscribe unsubscribe ping isConnected.
No brokerConnected local flag in fachclient.
No direct broker transport object usage in fachclient runtime.
No callback injection bypass like publishToBroker for broker communication path.
No side channel broker communication outside virtual interface contract.

Architecture law required shape
Fachclient implements only domain behavior and virtual interface contract.
Generic client drives runtime and invokes virtual interface.
Broker session lifecycle stays inside generic client only.

Report continuity rule
Before write report check if module report already exists.
If exists then extend same file.
Do not replace history.
Do not create duplicate report file for same module.
Only add delta findings new tasks new status updates as needed.

Hard rules
No silent failure.
No fake success log.
No swallow exceptions in runtime paths.
Deterministic error mapping.
Todo checkboxes update immediate.

Broker interplay mandatory in every pass
Must verify and test full lifecycle not one case.

Broker lifecycle checklist
connect success path
connect failure path
reconnect after transport loss
reconnect after broker restart
backoff behavior on repeated failures
resubscribe correctness after reconnect
unsubscribe correctness when needed
clean shutdown disconnect path
shutdown when disconnect throws

Session and protocol checklist
keepalive ping request response path
ping timeout handling and reconnect trigger
qos0 publish behavior
qos1 ack path puback
qos2 ack path pubrec pubrel pubcomp
ack timeout mapping with deterministic category
duplicate inbound handling dup flag
duplicate outbound retry handling
inflight message state correctness across reconnect
subscription activation only after broker confirm

Data integrity checklist
no false success log before broker confirm
no message loss in guaranteed path policy
no unbounded resend loop
retry queue bound and overflow behavior deterministic
retained flag correctness
ordering guarantees documented and tested
idempotent handling for retry duplicates

Network and runtime fault checklist
half open tcp behavior
read write exception handling
socket close race handling
dns failure handling if dns used
auth failure handling if auth used
broker unavailable long period behavior
process restart recovery behavior

Long run robustness checklist
no memory growth leak in reconnect loops
no thread or fd leak in reconnect loops
stable behavior under repeated connect disconnect cycles
soak style test or repeated cycle test exists
ci test uses deterministic bounded repeat cycle if full soak not possible

Observability checklist
structured logs for connect reconnect disconnect
structured logs for publish reject and ack timeout
structured logs for subscribe confirm fail
error logs include reason category and operation

Required broker checks
Each checklist item above must be marked done not done or out of scope in report.
Out of scope needs explicit reason.
Unit or integration test must exist for every done item.
If missing add tests in module scope or mqtt_client scope.
If module has no direct broker transport code verify via virtual interface contract runtime orchestration and shared mqtt_client tests.

Done gate
All non P2 todo done.
Architecture law checks complete.
Broker routine checklist complete.
Tests pass.
Diagnostics clean for changed files.