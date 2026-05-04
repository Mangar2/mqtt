# Project Instructions

Never call configure_python_environment. Never modify environments.
Use only existing system python use "python" for windows and python3 for linux or mac-os
It is strictly forbidden to create or extend code files of any type longer than 1000 lines  
All files code comments and documentation in **English**
Respond user language they write.
Keep project root always clean
Always exclude files in gitignore not suitable for github. Use generic rules and not per-file-rules.

## Skills

Caveman language all skills 
No format chars
No unneeded words
Skill files store .md in .claude/commands. Skill for AI only. No skill content here 
Only use skills from .claude/commands. Never read or follow skills from VS Code extensions or any path outside .claude/commands
Skills only here:

/build`: build project
/cpp-dev: C++20 rules, never violate, check after each created or changed file hat you followed all rules without any acception
/unit-test: unit testing
/integration-test: integration testing
/code-create: workflow coding
/github-pr: GitHub branches and Pull Requests
/bug-fixing: bug reproduction analysis and fix workflow
/yaha-spec: YAHA specification workflow
/yaha-log: YAHA logging workflow
/yaha-client-architektur: YAHA client main architecture workflow

## Project Plan

Pure MQTT broker activities:
Use implementation plan spec/implementation-plan.md.
Always consult never rename or move.
Defines broker modules sub-components dependency order.

YAHA activities:
Use spec/yaha/ as primary plan and specification source.
Do not use spec/implementation-plan.md as planning source for YAHA-only work.
In YAHA area follow spec/yaha documents and matching src/yaha/**/SPEC.md.

## Project Structure

mqtt/
├ src/          # All production source code (C++ .h / .cpp files)
├ tests/        # Top-level test entry point (test_placeholder.cpp)
├ spec/         # Project-level specification files (implementation-plan.md)
├ build/        # CMake build output — never read or modify manually
├ .claude/      # Skill files and settings
├ CMakeLists.txt
├ CMakePresets.json
└ CLAUDE.md

src/ modules organised as subdirectories mirroring implementation plan
Each directory has SPEC.md description — navigate via those files

Before coding read code-create.md skill.
