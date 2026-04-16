# reason_code — Module 1.2: Reason Codes

All 39 distinct MQTT 5.0 Reason Code numeric values (MQTT 5.0 Section 2.4 and Appendix B).

## Files

| File             | Contents |
|------------------|----------|
| `reason_code.h`  | `ReasonCode` enum (1.2.1) + `is_success` / `is_error` helpers (1.2.2) |

## Public API

### ReasonCode (enum class : uint8_t)

One enumerator per distinct wire value. Values that the spec uses under multiple names
(e.g. `0x00` = Success / Normal Disconnection / Granted QoS 0) are represented by
a single enumerator (`Success`) with `inline constexpr` aliases declared beneath the enum.

### Classification helpers (1.2.2)

```cpp
[[nodiscard]] constexpr bool is_success(ReasonCode rc) noexcept;
[[nodiscard]] constexpr bool is_error  (ReasonCode rc) noexcept;
```

- `is_success` returns `true` for codes in range [0x00, 0x1F].
- `is_error`   returns `true` for codes in range [0x80, 0xFF].
