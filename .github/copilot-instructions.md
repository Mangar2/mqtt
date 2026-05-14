# Project Instructions

Never call configure_python_environment. Never modify environments.
Use only existing system python: "python" on Windows, "python3" on Linux or macOS.
It is strictly forbidden to create or extend code files of any type longer than 1000 lines.
All files, code comments, and documentation in **English**.
Respond in the language the user writes in.
Keep project root always clean.
Always exclude files in .gitignore that are not suitable for GitHub. Use generic rules, not per-file rules.

## Skills

Skills are stored as .md files in .claude/commands/. Skills are for AI only.
Only use skills from .claude/commands. Never read or follow skills from VS Code extensions or any path outside .claude/commands.

Available skills:
- /build — build project
- /cpp-dev — C++20 rules, never violate; check after each created or changed file that all rules are followed without exception
- /unit-test — unit testing
- /integration-test — integration testing
- /code-create — workflow coding
- /github-pr — GitHub branches and Pull Requests
- /bug-fixing — bug reproduction, analysis and fix workflow
- /deployment — deployment workflow
- /refactoring — refactoring workflow
- /yaha-spec — YAHA specification workflow
- /yaha-log — YAHA logging workflow
- /yaha-client-architektur — YAHA client main architecture workflow

## Project Plan

Pure MQTT broker activities:
Use implementation plan spec/implementation-plan.md.
Always consult, never rename or move it.
Defines broker modules, sub-components, and dependency order.

YAHA activities:
Use spec/yaha/ as primary plan and specification source.
Do not use spec/implementation-plan.md as planning source for YAHA-only work.
In the YAHA area, follow spec/yaha documents and matching src/yaha/**/SPEC.md.

## Project Structure

```
mqtt/
├ src/          # All production source code (C++ .h / .cpp files)
├ tests/        # Top-level test entry point (test_placeholder.cpp)
├ spec/         # Project-level specification files (implementation-plan.md)
├ build/        # CMake build output — never read or modify manually
├ .claude/      # Skill files and settings
├ CMakeLists.txt
├ CMakePresets.json
└ CLAUDE.md
```

src/ modules are organised as subdirectories mirroring the implementation plan.
Each directory has a SPEC.md description — navigate via those files.

Before coding, read the code-create.md skill.

## User Preferences

- No workaround or throwaway implementations; partial scope is allowed only if the implemented part is fully correct, production-suitable, and at least at parity with original behavior for that part.
- Hard ban on simplified non-equivalent logic vs original; after implementation always verify parity and explicitly confirm no simplification was introduced.
- Test commands must be short and timeout-safe; avoid long-running waits; keep individual test runtime to about 1-2 seconds maximum.
- Never run all integration tests; run only integration tests related to the current project scope.
- Before any action outside the repository, explain it first and get agreement.
