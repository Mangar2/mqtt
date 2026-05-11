# OpenZWave Local Maintenance Changelog

This file tracks local maintenance changes applied to the vendored OpenZWave source.

## 2026-05-11

- Initial vendoring into `third_party/openzwave`.
- Pinned commit: `3fff11d246a0d558d26110e1db6bd634a1b347c0`.
- Removed nested upstream `.git` metadata to keep source repo-controlled.
- Local build-compatibility fix for AppleClang: added missing `override` annotations in
	- `cpp/src/command_classes/Supervision.h`
	- `cpp/src/command_classes/SwitchBinary.h`
	- `cpp/src/command_classes/SwitchMultilevel.h`
	- `cpp/src/command_classes/ThermostatMode.h`
	- `cpp/src/command_classes/ThermostatSetpoint.h`
- Local build-compatibility fix for AppleClang deprecation-as-error:
	- replaced `sprintf` with `snprintf` in `cpp/src/platform/HttpClient.cpp`.
- Phase-0 static build validation passed via CMake external target `openzwave_static_build`.
