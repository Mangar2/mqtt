# Pushover Client Config Test Specification

## Test cases

1. `load_runtime_config_parses_pushover_devices_and_subscriptions`
- Scenario: INI contains valid `[pushover]`, repeated `[device]`, and repeated `[subscription]` sections.
- Input: in-memory INI file.
- Expected: parser returns success with parsed host, credentials, devices, and subscriptions.

2. `load_config_requires_device_entries`
- Scenario: no `[device]` section exists.
- Input: INI with valid `[pushover]` and `[subscription]`.
- Expected: parser fails with missing device message.

3. `load_config_requires_subscription_entries`
- Scenario: no `[subscription]` section exists.
- Input: INI with valid `[pushover]` and `[device]`.
- Expected: parser fails with missing subscription message.

4. `load_config_requires_token_and_user`
- Scenario: required authentication keys are missing.
- Input: INI without `token` and `user`.
- Expected: parser fails with required key message.
