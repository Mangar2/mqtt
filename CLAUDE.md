# Project Instructions

## Language

All files, code, comments, and documentation must be written in **English**.
Respond to the user in the language they write in.

## Skills

**Skill files are stored as `.md` files in `.claude/commands/` (project root).**
- Skill files are written exclusively for AI consumption — be token-minimal: no prose, no examples, no redundancy.
- To create a new skill: create `.claude/commands/<skill-name>.md` — never put skill content inline in this file.
- This table is only the index. Never search for skill files elsewhere. Never check if the directory exists — it is always at `.claude/commands/`.
- When adding a new skill: create the file AND extend the table below.
- To reference an existing skill as a template: read `.claude/commands/build.md`.

| Command | When to use |
|---------|-------------|
| `/build` | Configure and compile the project |
| `/cpp-dev` | C++20 rules and code style |
| `/unit-test` | Run, write, or fix tests |
| `/code-create` | Full workflow for creating or extending code |

## Project Plan

The implementation plan is at `spec/implementation-plan.md`.
It defines all modules, their sub-components, and the dependency order.
Always consult it before starting any new module or feature. Never rename or move this file.

## Project Structure

Top-level layout (depth 1):

```
mqtt/
├── src/          # All production source code (C++ .h / .cpp files)
├── tests/        # Top-level test entry point (test_placeholder.cpp)
├── spec/         # Project-level specification files (implementation-plan.md)
├── build/        # CMake build output — never read or modify manually
├── .claude/      # Skill files and settings
├── CMakeLists.txt
├── CMakePresets.json
└── CLAUDE.md
```

Inside `src/`, modules are organised as subdirectories that mirror the implementation plan.
Each directory carries a `SPEC.md` as its authoritative description — navigate the tree via those files, not by listing directories.

**Before creating or extending any code: read the `/code-create` skill first.**
It defines all conventions (SPEC.md, TEST_SPEC.md, file count limits, test placement).

## Design rules policy

All **code design rules that are not specific to a single module** must be written in
`.claude/commands/cpp-dev.md` only (see the `## Project-wide conventions` section there).
Do **not** repeat general rules in `SPEC.md` files — only module-specific behaviour,
interfaces, and constraints belong in a module's `SPEC.md`.
