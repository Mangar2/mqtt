Skill for creating YAHA home automation component specifications step by step.
Caveman language. No format chars. No unneeded words.

## Purpose

Create specs for YAHA home automation components being reimplemented in C++.
Source: legacy JavaScript code under spec/@mangar2/
Target: spec/yaha/ — one SPEC.md per component

## Architecture principles to preserve

MQTT-client-as-interface pattern:
Component exposes getSubscriptions() and handleMessage() — mqtt client knows nothing about component internals. 
Component knows nothing about mqtt transport.
Main file composes both.
Interface is pure virtual in C++.

Each component is a standalone program (mqtt client + optional http server etc.).
Components combine: mqtt-client source + component source in one main.

## Workflow — execute steps in order

### Step 1 — Read existing code

Read all relevant files under spec/@mangar2/<component>/ to understand:
- what it does
- what it subscribes to
- what it publishes
- what external interfaces it exposes (http, serial, etc.)
- data structures and tree/storage organization
- persistence behavior
- configuration parameters
- error and cleanup behaviors

Also read related components (e.g. messagetree for messagestore) if referenced.

### Step 2 — Identify cross-cutting concerns

While reading, look for concepts that are not specific to this component:
- Reusable interfaces (e.g. IMqttComponent interface with getSubscriptions/handleMessage)
- Shared data structures (e.g. Message, Reason, History node)
- Shared patterns (persistence loop, configuration schema validation)
- Shared services (http server abstraction, persist service)

For each such concept: check if spec/yaha/SPEC-<name>.md already exists.
If not: create it alongside the component spec.

### Step 3 — Write the component SPEC

File: spec/yaha/SPEC-<component-name>.md

Structure:

# <Component Name>

## Purpose
What problem this component solves. One short paragraph.

## Role in the system
How it fits into YAHA. What it consumes, what it produces, who uses it.

## Standalone program structure
Describe this component as an independent program: what it is (mqtt client + http server etc.)
Describe how the main entry point composes mqtt client and component via interface.

## Subscriptions
List topic patterns it subscribes to and what each means.
Include QoS levels if significant.

## Published messages
List topics it publishes and when.

## External interfaces
HTTP endpoints, serial interfaces, REST API, etc.
For each: path, method, parameters, response format.

## Data model
Describe the core data structures and their relationships.
Mention tree architecture or key structural facts only when they are genuinely important to the spec.
Avoid implementation details. State shape and semantics, not code.

## Behavior
Key behaviors: message handling, query handling, special topics (e.g. $SYS/messages/cleanup).
State machine if relevant.

## Persistence
What is persisted, when, how often, retention/rotation policy.

## Configuration
Parameters with types, defaults, and meaning. No code — prose or table.

## Error handling
What errors are expected and how they are handled at behavior level.

## Architectural notes
Only include if genuinely constraining the implementation:
- Important data structure decisions (e.g. tree keyed by topic path segments)
- Interface contract that must be preserved (IMqttComponent)
- Any hard constraint (e.g. no external broker dependency, no framework X)

## Open questions
Things unclear from the legacy code that need a decision before implementation.

### Step 4 — Review the spec

After writing:
1. Re-read it and remove any line that contains an implementation detail disguised as a requirement.
2. Check: does every section answer "what" and "why", not "how"?
3. Check: are all cross-cutting concerns moved to their own SPEC files and only referenced here?
4. Check: is the standalone-program structure clear — how main composes mqtt client + component?

### Step 5 — Output to user

Summarize:
- what spec was created (file path)
- what cross-cutting SPEC files were created or already existed
- open questions found
- suggested next component to spec (based on dependencies)
