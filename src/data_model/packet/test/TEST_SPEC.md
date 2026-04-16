# TEST_SPEC — packet (Module 1.4)

## PacketType enum

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `pkt_type_connect_value` | Wire value | none | `Connect == 1` |
| `pkt_type_auth_value` | Last wire type | none | `Auth == 15` |

## ConnectPacket / WillData (1.4.1)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `connect_defaults` | Default-constructed packet | none | `clean_start == true, keep_alive == 0, no will, no username, no password` |
| `connect_with_will` | Packet with will | WillData set | `will.has_value() == true` |
| `connect_equality` | operator== | two identical packets | equal |

## ConnackPacket (1.4.2)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `connack_defaults` | Default-constructed | none | `session_present == false, reason_code == Success` |
| `connack_equality` | operator== | two identical packets | equal |

## PublishPacket (1.4.3)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `publish_defaults` | Default-constructed | none | `dup == false, qos == AtMostOnce, retain == false, no packet_id` |
| `publish_qos1_has_packet_id` | QoS 1 publish | packet_id set | `packet_id.has_value() == true` |
| `publish_equality` | operator== | two identical packets | equal |

## ACK packets: PUBACK, PUBREC, PUBREL, PUBCOMP (1.4.4–1.4.7)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `puback_defaults` | Default-constructed | none | `packet_id == 0, reason_code == Success` |
| `pubrec_defaults` | Default-constructed | none | same |
| `pubrel_defaults` | Default-constructed | none | same |
| `pubcomp_defaults` | Default-constructed | none | same |

## SubscribePacket + SubackPacket (1.4.8–1.4.9)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `subscribe_defaults` | Default-constructed | none | `packet_id == 0, filters empty` |
| `subscribe_options_defaults` | Default SubscriptionOptions | none | `max_qos == AtMostOnce, no_local == false, retain_as_published == false, retain_handling == 0` |
| `suback_defaults` | Default-constructed | none | `packet_id == 0, reason_codes empty` |

## UnsubscribePacket + UnsubackPacket (1.4.10–1.4.11)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `unsubscribe_defaults` | Default-constructed | none | `packet_id == 0, topic_filters empty` |
| `unsuback_defaults` | Default-constructed | none | `packet_id == 0, reason_codes empty` |

## PINGREQ / PINGRESP (1.4.12–1.4.13)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `pingreq_equality` | operator== on empty struct | two instances | equal |
| `pingresp_equality` | operator== on empty struct | two instances | equal |

## DisconnectPacket (1.4.14)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `disconnect_defaults` | Default-constructed | none | `reason_code == Success` |

## AuthPacket (1.4.15)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `auth_defaults` | Default-constructed | none | `reason_code == Success` |
