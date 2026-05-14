# Implementation Plan: ZWave Client 1.0

This plan defines the implementation sequence for [SPEC-zwave.md](./SPEC-zwave.md).

The scope is a new standalone YAHA ZWave client in C++ with strict parity to legacy executable behavior and full deployment integration.

## Goal

Implement one coherent ZWave standalone client that:
- bridges MQTT control/set topics and Z-Wave operations
- maps Z-Wave events and values to MQTT messages
- preserves legacy functional mapping, conversion, and fallback behavior with approved scan-fix deviation
- ships as build target plus deployment package component

## Scope

In scope:
- ZWave domain component implementing IMqttComponent
- controller adapter behavior matching legacy OpenZWave callback contract
- device mapping helper behavior parity
- standalone runtime config mapping and executable composition
- strict parity unit/component tests versus legacy behavior
- deployment integration via create_yaha_deployment.py and deploy_yaha_scp.py
- local vendored OpenZWave source with pinned version and static-link integration

Out of scope:
- broker runtime transport changes
- modifications to unrelated YAHA components
- behavioral simplification of any legacy ZWave path

## Referenced specifications

- [SPEC-zwave.md](./SPEC-zwave.md)
- [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md)
- [SPEC-message.md](./SPEC-message.md)

## Target module structure

Create three YAHA modules.

1. Domain module: src/yaha/zwave/
- zwave config type
- service orchestration component implementation (IMqttComponent)
- command dispatch and subscription derivation
- publish callback handoff and message matching integration

2. Adapter module: src/yaha/zwave_controller/
- controller runtime wrapper and callback registration
- node/value cache behavior
- event-to-message mapping behavior
- value set/config set operations

3. Mapping module: src/yaha/zwave_devices/
- valueToTopicAndType resolution
- topicToZwaveId resolution and default completion
- value-id cache behavior

Runtime module:
- src/yaha/zwave_client/
- INI config loader and composition helpers

Standalone entry point:
- src/yaha_zwaveclient_main.cpp

## Mandatory output artifacts

Implementation is complete only when all artifacts below exist and are wired.

1. Build/runtime artifacts
- executable yahazwaveclient
- standalone main src/yaha_zwaveclient_main.cpp
- CMake target and install integration in CMakeLists.txt

2. Third-party stack artifacts
- local OpenZWave source under repository third-party path
- pinned OpenZWave commit/tag recorded in build docs/config
- static-link integration so target runtime has no external OpenZWave package dependency

3. Runtime config artifacts
- template cmake/ini/zwave.ini
- parser support for required schema and defaults
- explicit validation for required keys and QoS enums

4. Deployment artifacts
- create_yaha_deployment.py includes zwave component with:
  - binary yahazwaveclient
  - ini zwave.ini
  - service unit yahazwaveclient.service
  - install command wiring
- deploy_yaha_scp.py supports deploying/installing zwave component
- deployment output contains deployment/yaha/zwave/zwave.ini

5. Quality artifacts
- strict parity test suite for mapping and event contracts
- focused integration tests for command routing and publish flow
- operational startup/runtime logging

## Implementation phases

## Phase 0: OpenZWave source acquisition and build wiring

Step 0. Vendor OpenZWave source locally
- use upstream source repository `https://github.com/OpenZWave/open-zwave.git`
- use fixed local source path `third_party/openzwave`
- clone OpenZWave source into that location
- keep source locally in repo-managed workspace for in-house maintenance fixes

Step 0.1 Pin OpenZWave version
- pin by full commit hash (40 hex chars), not branch name
- record pin in `third_party/openzwave/PINNED_VERSION.txt`
- `PINNED_VERSION.txt` must include upstream URL, commit hash, and vendoring date
- forbid floating version resolution

Step 0.2 Integrate OpenZWave static build
- add static build wiring in CMake
- link statically into yahazwaveclient target
- ensure runtime has no dependency on external system OpenZWave package

Step 0.3 Third-party compliance and update policy
- record upstream source origin and license metadata in `spec/third_party/openzwave/NOTICE.md`
- keep local patch/change log in `spec/third_party/openzwave/CHANGELOG.md`
- define update workflow: pin change requires full parity and deployment verification
- define update workflow: every local OpenZWave source change requires local changelog entry and pinned commit update

Deliverable:
- reproducible local OpenZWave source + pinned static-link integration

## Phase 1: Contracts and config mapping

Step 1. Define ZWave config contract
- model fields: subscribeQoS, qos, retain, usb, devices
- model device entries: topic, node_id, optional class_id/instance/index/type/label
- preserve defaults: subscribeQoS=1, qos=1, retain=false
- reject unknown top-level properties

Step 2. Implement runtime INI mapping
- parse domain config and mqtt runtime config
- keep deterministic field-specific error messages
- preserve no-hidden-fallback validation semantics

Deliverable:
- valid domain and runtime config objects

## Phase 2: Mapping and conversion layer

Step 3. Implement valueToTopicAndType behavior parity
- best-match scoring by class_id/instance/index specificity
- label append rule when mapped config omits class_id
- value_id cache behavior

Step 4. Implement topicToZwaveId behavior parity
- lookup order `<topic>/<label>` then `<topic>`
- default completion rules (instance/index/type)
- class_id resolution by label search in node tree when absent

Step 5. Implement set-value conversion behavior
- bool/switch conversion truth table parity
- byte numeric conversion
- config class (`0x70`) routes to setConfigParam path

Deliverable:
- mapping and conversion behavior fully parity-aligned

## Phase 3: Controller and service behavior

Step 6. Implement controller event contract
- driver ready/failed, scan complete
- notification mapping table behavior
- controller command feedback publish behavior
- node added/node ready/value added/value removed/value changed/value refreshed behavior

Step 7. Implement service publish and matcher flow
- route controller publishes through reply matcher update
- enforce configured qos/retain on outbound messages
- preserve reasons and fallback topics

Step 8. Implement inbound MQTT routing behavior
- management topics:
  - `$MONITOR/zwave/removefailednode/set`
  - `$MONITOR/zwave/addnode/set`
  - `$MONITOR/zwave/scan/set`
- default path for regular set topics to controller setValue
- implement approved scan fix behavior from spec with deterministic success/failure publish contract

Step 9. Implement run and close behavior
- run publishes restart marker messages
- run requests config parameters for all nodes
- close disconnects controller

Deliverable:
- full domain component behavior parity

## Phase 4: Standalone client creation

Step 10. Implement client runtime composition
- create ZWave component from mapped config
- create MQTT client runtime and wire IMqttComponent boundary
- wire publish callback and inbound dispatch

Step 11. Implement thin main
- add src/yaha_zwaveclient_main.cpp
- parse args and config path
- load and map config
- compose component + mqtt runtime + generic runtime
- run once and return exit status

Architecture checks:
- no runtime policy in main
- no reconnect/signal policy in main
- no duplicate generic parser/runtime orchestration logic

Deliverable:
- runnable yahazwaveclient with clean YAHA architecture boundaries

## Phase 5: Mandatory parity verification

Step 12. Build parity harness against legacy JS behavior
- execute deterministic legacy scenarios for mapping/event flows
- record canonical outputs for comparisons

Step 13. Implement mapping parity tests
- matrix coverage for class/instance/index matching cases
- cache-hit and cache-miss behavior
- topic lookup + default completion cases

Step 14. Implement event contract parity tests
- each controller callback path and expected message outputs
- fallback/error path outputs and reasons
- node cache mutation behavior per event

Step 15. Implement command routing parity tests
- control-topic dispatch behavior
- regular set-topic behavior
- dedicated tests for approved scan fix behavior

Step 16. Enforce zero-delta gate
- any field mismatch in topic/value/reason/qos/retain/fallback path fails test for all non-approved deviation areas
- parity suite blocks merge and release

Deliverable:
- gapless parity proof for legacy ZWave behavior

## Phase 6: Integration verification

Step 17. Unit tests
- config schema/default validation
- mapping helpers and conversion logic
- subscription derivation behavior

Step 18. Component tests
- inbound MQTT to controller operation mapping
- controller publish to outbound MQTT with matcher/qos/retain
- startup run behavior and startup marker publishes

Step 19. Runtime integration tests
- startup and shutdown lifecycle
- config load errors and validation handling
- runtime publish path integrity

Deliverable:
- deterministic runtime behavior validated end-to-end

## Phase 7: Build and deployment integration

Step 20. CMake integration
- add target yahazwaveclient in CMakeLists.txt
- wire include/link/install consistent with existing YAHA clients

Step 21. Deployment packaging integration
- add zwave component packaging in cmake/create_yaha_deployment.py
- include binary + ini + service assets + install wiring

Step 22. Remote deployment integration
- extend cmake/deploy_yaha_scp.py for zwave component
- verify install-root and no-overwrite-ini behavior

Step 23. Deployment verification commands
- build package command:
  - python3 cmake/create_yaha_deployment.py --build --preset armv7-zig-release
- deploy command:
  - python3 cmake/deploy_yaha_scp.py --remote-host <host> --remote-dir mqtt --install-root --no-overwrite-ini
- artifact check:
  - verify deployment/yaha/zwave contains binary, ini, and service assets

Deliverable:
- end-to-end build, package, deploy flow for ZWave client

## Acceptance criteria (100% done)

1. yahazwaveclient builds and runs from INI config.
2. ZWave behavior matches legacy executable behavior with zero-delta parity gate.
3. Any parity mismatch outside approved scan-fix deviation fails verification and blocks release.
4. IMqttComponent architecture boundary is preserved and main stays thin.
5. Subscription, routing, mapping, and conversion behaviors match spec.
6. CMake target and install wiring are integrated.
7. create_yaha_deployment.py includes zwave packaging.
8. deploy_yaha_scp.py supports zwave deployment and install.
9. Deployment output contains zwave binary, ini, and service artifacts.
10. OpenZWave source is locally vendored, pinned, and statically linked into the client.

## Risks and implementation notes

1. Mapping ambiguity for partial device definitions
- mitigate with exhaustive matching matrix tests and golden-reference checks.

2. Drift in legacy fallback/error behavior
- enforce fallback-path assertions in parity suite.

3. External-driver callback ordering differences
- separate deterministic unit-level callback contract tests from integration tests.

4. Deployment naming drift across scripts/services
- define one canonical component/binary/service naming set and verify in packaging tests.

## Step execution order summary

Step 0 -> Step 0.1 -> Step 0.2 -> Step 0.3 -> Step 1 -> Step 2 -> Step 3 -> Step 4 -> Step 5 -> Step 6 -> Step 7 -> Step 8 -> Step 9 -> Step 10 -> Step 11 -> Step 12 -> Step 13 -> Step 14 -> Step 15 -> Step 16 -> Step 17 -> Step 18 -> Step 19 -> Step 20 -> Step 21 -> Step 22 -> Step 23
