Skill for zwave service robustness.
Caveman language. No format chars. No unneeded words.

Purpose
Make zwave service robust.
No silent failure.
No fake success.

Scope
src/yaha/zwave/
src/yaha/zwave_client/

Flow
1 analyze failure branches
2 create todo report
3 implement all non-P2 fixes
4 update tests and specs
5 run diagnostics and client coverage

Must fix
- publish callback failures must be observable
- controller exception paths must not be silent
- startup and shutdown controller failures must be logged deterministically

Done gate
- non-P2 todo items done
- tests pass
- docs reflect behavior