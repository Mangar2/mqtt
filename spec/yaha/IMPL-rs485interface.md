# Implementation Plan: RS485Interface Client 1.0

This plan defines the implementation sequence for [SPEC-rs485interface.md](./SPEC-rs485interface.md).

The scope is a new standalone YAHA RS485Interface client in C++ with strict behavioral parity to legacy RS485 state logic, plus full deployment integration.

## Goal

Implement one coherent RS485Interface standalone client that:
- bridges MQTT commands and RS485 serial protocol frames
- runs token-based bus coordination with strict RS485 state-machine parity
- publishes mapped MQTT status/state messages from serial input
- supports deterministic startup, runtime, shutdown, packaging, and deployment

## Scope

In scope:
- RS485Interface domain component implementing IMqttComponent
- serial protocol frame codec and stream parser
- token scheduler and send queue behavior
- topic mapping (MQTT <-> serial)
- standalone runtime config mapping and executable composition
- full parity unit-test suite against legacy rs485state.js behavior
- deployment integration via create_yaha_deployment.py and deploy_yaha_scp.py

Out of scope:
- changes to unrelated YAHA components
- broker runtime transport policy changes
- functional relaxation of any legacy RS485 state behavior

## Referenced specifications

- [SPEC-rs485interface.md](./SPEC-rs485interface.md)
- [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md)
- [SPEC-message.md](./SPEC-message.md)

## Target module structure

Create four YAHA modules.

1. Domain module: src/yaha/rs485_interface/
- domain config type
- RS485Interface component implementation (IMqttComponent)
- action handling (/set, /temporary, /blink)
- subscription derivation and publish mapping

2. Protocol module: src/yaha/rs485_protocol/
- serial message frame encode/decode (v0/v1)
- CRC/parity utilities
- serial stream read and noise skipping
- constants and protocol value definitions

3. Token/state module: src/yaha/rs485_state/
- RS485State state machine
- RS485TokenExchange logic
- address chain tracking
- scheduler-ready state signaling

4. Runtime module: src/yaha/rs485_interface_client/
- INI runtime config loader and mapping
- serial adapter integration
- component + MQTT runtime composition helpers

Standalone entry point:
- src/yaha_rs485interfaceclient_main.cpp

## Mandatory output artifacts

Implementation is complete only when all artifacts below exist and are wired.

1. Build and runtime artifacts
- executable yahars485interfaceclient
- standalone main src/yaha_rs485interfaceclient_main.cpp
- CMake target and install integration in CMakeLists.txt

2. Runtime config artifacts
- template cmake/ini/rs485interface.ini
- parser support for all documented config keys
- no hidden fallback that weakens required validation

3. Deployment artifacts
- create_yaha_deployment.py includes component rs485interface with:
  - binary yahars485interfaceclient
  - ini rs485interface.ini
  - service unit yahars485interfaceclient.service
  - install command wiring
- deploy_yaha_scp.py supports copy/install of rs485interface component
- deployment output contains:
  - deployment/yaha/rs485interface/rs485interface.ini

4. Quality artifacts
- strict parity unit-test suite for RS485 state machine
- focused component/integration tests for runtime behavior and mapping
- operational startup/runtime logging for diagnostics

## Implementation phases

## Phase 1: Contracts and configuration mapping

Step 1. Define RS485Interface config contract
- model all required keys from spec:
  - serialPortName, baudrate, myAddress, maxVersion, tickDelay
  - timeOfDayDelayInSeconds, qos, trace
  - blinkDelayInSeconds, temporaryOnInSeconds
  - interfaces, settings, status, addresses, topics
- enforce ranges and enum values
- reject invalid or incomplete input with deterministic errors

Step 2. Implement runtime INI mapping
- map INI sections into domain config and MQTT runtime config
- apply explicit defaults from spec only where allowed
- keep parser diagnostics field-specific and deterministic

Deliverable:
- valid runtime config object for component + MQTT runtime

## Phase 2: Protocol and mapping layer

Step 3. Implement serial message codec
- support version 0 and version 1 frame handling exactly
- preserve parity/CRC checks and failure behavior
- keep command/value encoding and decoding contract

Step 4. Implement serial stream reader
- skip noise bytes exactly as specified
- parse multiple frames from one byte chunk
- preserve per-frame error reporting behavior

Step 5. Implement MQTT <-> serial mapper
- topic-to-address and topic-to-command resolution
- interface string-to-value mapping and reverse mapping
- explicit topic mapping behavior via topics object
- strict error behavior for unknown address/command/value mapping

Deliverable:
- protocol and mapper behavior fully aligned with specification

## Phase 3: RS485 state machine and scheduler

Step 6. Implement RS485State with exact transition behavior
- states: unknown, reboot, single, unregistered, registered
- loop events and token events with exact transition rules
- side effects for maySend, sibling tracking, timer handling

Step 7. Implement token exchange behavior
- state signaling message generation
- right/left sibling and address chain updates
- version downgrade rules tied to enable-send behavior

Step 8. Implement send queue and tick scheduler
- queue replacement rule excluding command X
- send retry semantics and dequeue rules
- per-tick ordering:
  - process state without message
  - optional queue send when maySend
  - reset maySend to avoid duplicate send per tick

Deliverable:
- deterministic token and scheduler behavior

## Phase 4: Domain component and standalone client creation

Step 9. Implement RS485Interface component (IMqttComponent boundary)
- getSubscriptions from settings/status/topics/addresses + monitoring namespace
- handleMessage behavior for trace update and action processing
- outbound publish callback integration
- serial receive pipeline to publish mapped MQTT messages

Step 10. Implement standalone client runtime composition
- create component instance from mapped config
- create MQTT client runtime and serial adapter
- wire publish callback and inbound dispatch path
- run lifecycle through generic runtime orchestration

Step 11. Add standalone main
- implement thin main in src/yaha_rs485interfaceclient_main.cpp
- parse args, load config, map config, compose runtime, run once
- keep main free of reconnect/signal/runtime policy logic

Deliverable:
- runnable yahars485interfaceclient process with clean boundaries

## Phase 5: Mandatory parity verification

Step 12. Build golden-reference parity harness
- execute legacy rs485state.js as reference oracle
- define canonical input event model for transition replay
- compare state, outputs, and side effects at each step

Step 13. Implement complete transition-matrix unit tests
- every state
- every input class
- addressed/not-addressed dimensions
- timer and sibling precondition combinations
- expected state outputs and side effects

Step 14. Implement long deterministic replay tests
- mixed event sequences including no-message loop events
- token-loss scenarios and recovery paths
- version and chain side-effect scenarios

Step 15. Enforce zero-delta gate
- one mismatching field at one step fails test run
- no tolerance and no exception path
- parity suite blocks merge and release

Deliverable:
- gapless parity proof for RS485 state-machine behavior

## Phase 6: Component and integration verification

Step 16. Unit tests
- config parsing and validation
- serial codec positive/negative cases
- reader noise and framing behavior
- mapper conversion rules and error behavior
- queue replacement and retry semantics

Step 17. Component tests
- MQTT input to serial output for set/temporary/blink
- serial input to MQTT publish mapping
- trace topic update behavior on monitoring namespace
- subscription derivation contract

Step 18. Runtime integration tests
- startup and clean shutdown paths
- serial reconnect behavior on send/open failure
- scheduler tick execution and queue drain behavior

Deliverable:
- deterministic runtime behavior proven against spec

## Phase 7: Build and deployment integration

Step 19. CMake integration
- add target yahars485interfaceclient to CMakeLists.txt
- include source sets and include paths
- add install rules consistent with existing YAHA clients

Step 20. Deployment packaging integration
- add rs485interface component packaging in cmake/create_yaha_deployment.py
- include binary, ini, service file, install command
- ensure deployment output path and generated artifacts are complete

Step 21. Remote deployment integration
- extend cmake/deploy_yaha_scp.py component handling for rs485interface
- verify install-root and no-overwrite-ini behavior for new component
- verify component deployment on target host

Step 22. Deployment verification commands
- build package command:
  - python3 cmake/create_yaha_deployment.py --build --preset armv7-zig-release
- deploy command:
  - python3 cmake/deploy_yaha_scp.py --remote-host <host> --remote-dir mqtt --install-root --no-overwrite-ini
- artifact check:
  - verify deployment/yaha/rs485interface contains binary, ini, service assets

Deliverable:
- end-to-end build, package, deploy flow for RS485Interface client

## Acceptance criteria (100% done)

1. yahars485interfaceclient builds and runs from INI config.
2. RS485State behavior is proven bit-for-bit equivalent to legacy rs485state.js behavior by mandatory parity tests.
3. One parity mismatch at any step fails verification.
4. IMqttComponent boundary is preserved and main remains thin composition only.
5. Subscription and publish behavior match specification including monitoring namespace mapping.
6. Serial protocol handling (v0/v1, CRC/parity, reader behavior) matches specification.
7. CMake target and install rules are integrated.
8. create_yaha_deployment.py packages rs485interface artifacts.
9. deploy_yaha_scp.py deploys and installs rs485interface artifacts.
10. Deployment output contains rs485interface binary, ini, and service assets.

## Risks and implementation notes

1. Hidden timing drift in ported state machine
- mitigate with exhaustive matrix + replay parity tests as release gate.

2. Ambiguous legacy edge behavior
- preserve behavior exactly as observed from executable legacy code and parity harness.

3. Deployment drift across script, service name, and binary name
- keep one canonical component name and verify in packaging tests.

4. Runtime race between serial input and tick loop
- enforce deterministic synchronization around shared scheduler/state paths.

## Step execution order summary

Step 1 -> Step 2 -> Step 3 -> Step 4 -> Step 5 -> Step 6 -> Step 7 -> Step 8 -> Step 9 -> Step 10 -> Step 11 -> Step 12 -> Step 13 -> Step 14 -> Step 15 -> Step 16 -> Step 17 -> Step 18 -> Step 19 -> Step 20 -> Step 21 -> Step 22
