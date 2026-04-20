# TEST_SPEC — types (Module 1.1)

## VariableByteInteger

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `vbi_default_value` | Default-constructed value | none | `value == 0` |
| `vbi_encoded_size_1byte` | Value in [0, 127] | `value = 0`, `127` | `encoded_size() == 1` |
| `vbi_encoded_size_2byte` | Value in [128, 16383] | `value = 128`, `16383` | `encoded_size() == 2` |
| `vbi_encoded_size_3byte` | Value in [16384, 2097151] | `value = 16384`, `2097151` | `encoded_size() == 3` |
| `vbi_encoded_size_4byte` | Value in [2097152, 268435455] | `value = 2097152`, `268435455` | `encoded_size() == 4` |
| `vbi_max_value_constant` | k_max_value sentinel | none | `k_max_value == 268435455` |
| `vbi_equality` | operator== | two structs with same value | equal |

## Utf8String / Utf8StringPair

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `utf8_default_empty` | Default-constructed | none | `value.empty()` |
| `utf8_max_byte_length` | Constant | none | `k_max_byte_length == 65535` |
| `utf8_equality` | operator== | two equal strings | equal |
| `utf8_pair_equality` | operator== on pair | same name+value | equal |

## BinaryData

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `binary_default_empty` | Default-constructed | none | `data.empty()` |
| `binary_max_byte_length` | Constant | none | `k_max_byte_length == 65535` |
| `binary_from_string_converts_text_bytes` | Helper converts text to byte payload | `"abc"` | bytes `{0x61,0x62,0x63}` |
| `binary_equality` | operator== | two equal payloads | equal |

## Integers

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `two_byte_size` | sizeof | none | `sizeof(TwoByteInteger) == 2` |
| `four_byte_size` | sizeof | none | `sizeof(FourByteInteger) == 4` |

## QoS

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `qos_values` | Underlying numeric values | none | `AtMostOnce==0, AtLeastOnce==1, ExactlyOnce==2` |
