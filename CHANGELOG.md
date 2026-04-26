# Changelog

All notable changes to this project will be documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Changed
- Renamed broker tool/binary and related build/test target names from `mqtt_broker`/`mqtt-broker` to `yahabroker`.

## [0.1.0] – 2026-04-26

### Added
- Initial stable release of a C++20 MQTT 5.0 broker
- Modular architecture covering core domains (`codec`, `connection`, `message_router`, `session_manager`, `subscription_manager`, `store`, `persistence`, `monitoring`, and more)
- MQTT 5 packet handling for CONNECT/CONNACK, PUBLISH flow, subscribe/unsubscribe flow, keep-alive, and disconnect handling
- Topic processing engine with topic validation and wildcard matching support
- Session lifecycle management including resume/takeover behavior and expiry control
- QoS runtime components for reliable delivery workflows
- Retained message handling and offline queueing support
- Configurable authentication and authorization modules
- Structured tracing system with global level and per-module trace filters
- Persistence subsystem with configurable modes (`full`, `off`, `no-states`) and data directory selection
- INI configuration parser with validation and deterministic startup precedence (defaults -> config -> CLI)
- Command-line interface with config loading, config validation mode, trace overrides, and help output
- TCP listener support for MQTT and optional plain WebSocket listener
- Build system with CMake + Ninja workflow for host and ARM targets
- Presets for `debug`, `release`, `debug-sanitize`, `armv6-*`, and `arm-*` builds
- Toolchain support for Raspberry Pi 32-bit cross-compilation scenarios
- Extensive automated verification with more than 1000 unit tests and more than 300 Python integration tests
- Performance test tooling and benchmark runner scripts under `test/`

### Changed
- Finalized and stabilized module boundaries according to the implementation plan
- Unified runtime configuration surface across broker, network, auth, ACL, persistence, and tracing sections
- Hardened startup behavior to fail fast on invalid ranges and unknown CLI flags
- Improved operational diagnostics with structured trace themes and module-level filtering

### Fixed
- Addressed protocol and runtime edge cases uncovered during large-scale unit and integration test runs
- Improved reliability of connection/session transitions under reconnect and takeover situations
- Improved robustness of queue/backpressure handling in high-throughput scenarios

### Security
- Anonymous access is configurable and can be disabled for credential-only deployments
- ACL rule support for topic-scoped publish/subscribe access control

### Documentation
- Added comprehensive project README with run/build instructions, full CLI option reference, and complete INI configuration reference
- Added and maintained a detailed MQTT broker implementation plan in `spec/implementation-plan.md`

### Notes
- This is the first functional release line (`0.1.x`) and establishes the public baseline for subsequent feature and hardening iterations
- TLS/MQTTS and WSS are not part of this release
- MQTT 3.x protocol downgrade/compatibility mode is not part of this release (MQTT 5 only)
