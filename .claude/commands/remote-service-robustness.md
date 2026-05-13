Skill for remote service robustness.
Caveman language. No format chars. No unneeded words.

Purpose
Make remote service robust.
No silent failure.
No fake success.

Scope
src/yaha/remote_service/
src/yaha/remote_service_client/
src/yaha/remote_service_http/ only if needed for contract parity.

Flow
1 analyze runtime failure branches
2 create todo report
3 implement all non-P2 fixes
4 update specs and tests
5 run diagnostics and client coverage

Must fix
- publishCommand failure paths must be observable
- startup and reload failure logs must be deterministic
- runtime HTTP listen failure must be observable

Done gate
- non-P2 report items done
- tests pass
- docs reflect behavior