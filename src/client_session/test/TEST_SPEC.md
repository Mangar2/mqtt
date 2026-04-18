# client_session/test — Unit Test Specification (Module 21)

## ClientSession construction and accessors

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `client_session_ctor_sets_connected_and_exposes_accessors` | Construct post-CONNECT context | client_id, username, queue, store | accessors return values; state is Connected |

## Inbound handlers

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `on_publish_qos0_returns_routable_message_without_ack` | QoS 0 inbound publish | PUBLISH QoS0 | routable message present, no response frames |
| `on_publish_qos1_returns_puback_frame` | QoS 1 inbound publish | PUBLISH QoS1 with packet_id | one response frame decoded as PUBACK with matching packet_id |
| `on_publish_qos2_duplicate_suppresses_routing` | QoS 2 duplicate detection | same QoS2 PUBLISH twice | first call has routable message; second call no routable message; both return PUBREC |
| `on_pubrel_returns_pubcomp_frame_for_inbound_qos2` | QoS 2 inbound completion | on_publish(QoS2) then PUBREL | returns PUBCOMP frame with same packet_id |

## Outbound drain and QoS progression

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `drain_outbound_qos0_encodes_publish_without_packet_id` | QoS0 queued message | queue push QoS0 | one encoded PUBLISH, qos==0, packet_id absent |
| `drain_outbound_receive_maximum_pauses_and_resumes_after_puback` | QoS1 flow control | receive_max=1, queue two QoS1 messages | first drain emits one packet; second emits none; after on_puback third emits second packet |
| `on_pubrec_returns_pubrel_and_on_pubcomp_releases_slot` | QoS2 outbound progression | queue QoS2, then PUBREC/PUBCOMP | on_pubrec returns PUBREL; after on_pubcomp second queued QoS2 can be emitted |

## AUTH handling

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `on_auth_routes_to_reauthenticate_when_reason_is_reauthenticate` | Re-auth path | initiate enhanced auth then AUTH reason ReAuthenticate | returns authenticator result from authenticate callback |
