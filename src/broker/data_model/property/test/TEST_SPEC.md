# TEST_SPEC — property (Module 1.3)

## PropertyId enum (1.3.1)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `prop_id_payload_format` | Wire value | none | `0x01` |
| `prop_id_user_property` | Wire value | none | `0x26` |
| `prop_id_shared_sub_available` | Wire value of last ID | none | `0x2A` |

## Property struct

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `property_equality` | operator== | two identical Property structs | equal |
| `property_inequality` | operator== | structs with different IDs | not equal |

## property_data_type mapping (1.3.2)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `prop_type_byte` | Byte-typed properties | PayloadFormatIndicator, MaximumQoS, RetainAvailable | `Byte` |
| `prop_type_two_byte` | TwoByteInteger-typed | ServerKeepAlive, ReceiveMaximum, TopicAliasMaximum, TopicAlias | `TwoByteInteger` |
| `prop_type_four_byte` | FourByteInteger-typed | MessageExpiryInterval, SessionExpiryInterval, WillDelayInterval, MaximumPacketSize | `FourByteInteger` |
| `prop_type_vbi` | VariableByteInteger | SubscriptionIdentifier | `VariableByteInteger` |
| `prop_type_utf8` | Utf8String-typed | ContentType, ResponseTopic, AssignedClientIdentifier | `Utf8String` |
| `prop_type_utf8_pair` | Utf8StringPair | UserProperty | `Utf8StringPair` |
| `prop_type_binary` | BinaryData-typed | CorrelationData, AuthenticationData | `BinaryData` |

## is_property_allowed mapping (1.3.3)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `prop_allowed_payload_format_in_publish` | Allowed | PayloadFormatIndicator, Publish | true |
| `prop_allowed_payload_format_in_connect` | Not allowed | PayloadFormatIndicator, Connect | false |
| `prop_allowed_user_prop_everywhere` | Allowed in all non-ping packets | UserProperty + each allowed type | true |
| `prop_allowed_topic_alias_only_publish` | Only in Publish | TopicAlias, Subscribe | false |
| `prop_allowed_will_delay_only_will` | Only in Will context | WillDelayInterval, Publish | false |
| `prop_allowed_will_delay_in_will` | Allowed in Will | WillDelayInterval, Will | true |
| `prop_allowed_session_expiry_multi` | Allowed in Connect/Connack/Disconnect | SessionExpiryInterval | true for all three |
| `prop_allowed_publish_or_will_in_will` | `|| Will` branch via Will context | MessageExpiryInterval, ContentType, ResponseTopic, CorrelationData with Will | true |
| `prop_allowed_auth_false_path` | Connack+Auth branch via wrong packet | AuthenticationMethod, AuthenticationData with Publish | false |
| `prop_allowed_reason_and_server_ref_false_path` | All branches via non-matching packet | ReasonString with Connect, ServerReference with Publish | false |
