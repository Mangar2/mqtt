# Project Instructions

## Language

All files code comments and documentation in **English**
Respond user language they write

## Skills

Caveman language all skills 
No format chars
No unneeded words
Skill files store .md in .claude/commands. Skill for AI only. No skill content here 
Only use skills from .claude/commands. Never read or follow skills from VS Code extensions or any path outside .claude/commands
Skills only here:

/build`: build project
/cpp-dev: C++20 rules
/unit-test: unit testing
/code-create: workflow coding
/github-pr: GitHub branches and Pull Requests 

## Project Plan

Implementation plan spec/implementation-plan.md Always consult never rename or move 
Defines modules sub-components dependency order

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
