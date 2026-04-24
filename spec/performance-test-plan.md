# MQTT Broker – Performance Test Plan

## Overview

This plan defines 10 new performance test scenarios (P01–P10) as additions to the existing S01
connect-ramp test. S01 stays unchanged.

### Common design rules

- Every scenario has a **ramp phase** and a **hold phase**.
- Ramp phase: load starts at `start_load`, increases by `step` every `ramp_interval_s` seconds
  until `end_load` is reached.
- Hold phase: peak load is maintained for `hold_seconds`.
- Total test time = ramp_duration + hold_seconds ≈ 2 minutes by default.
  When the runner is given a longer `--duration` parameter, only `hold_seconds` grows.
- All parameters are defined as plain Python constants at the top of the scenario function,
  grouped by size profile (`small`, `middle`, `large`).
- Each scenario covers **one theme** only (one MQTT feature or traffic pattern).
- Success criteria is measured against the hold phase only (ramp may have lower rates).

### Size profiles

| profile | typical connections | msg rate / s | notes            |
|---------|--------------------:|-------------:|------------------|
| small   | ≤ 20                | ≤ 500        | local / CI       |
| middle  | ≤ 100               | ≤ 5 000      | LAN / RPi        |
| large   | ≤ 500               | ≤ 50 000     | dedicated server |

---

## P01 – QoS0 Message Rate Ramp

**Theme:** QoS0 publish-and-deliver throughput under increasing message rate.

**What it does:**
One publisher and one subscriber are connected to the same topic. The publisher sends messages
at an increasing rate. At peak rate the load is held for `hold_seconds`. Delivered message count
is tracked end-to-end.

**Parameters (editable at top of function):**

```
# --- P01 parameters ---
PARAMS_P01 = {
    "small":  {"start_msg_s": 10,  "end_msg_s": 200,  "step": 10,  "ramp_interval_s": 3.0, "hold_seconds": 30},
    "middle": {"start_msg_s": 50,  "end_msg_s": 2000, "step": 50,  "ramp_interval_s": 2.0, "hold_seconds": 30},
    "large":  {"start_msg_s": 200, "end_msg_s": 20000,"step": 200, "ramp_interval_s": 1.0, "hold_seconds": 30},
}
```

**Ramp logic:** publish `start_msg_s` messages in the first second, wait `ramp_interval_s`,
add `step`, repeat until `end_msg_s`.

**Success criteria:** ≥ 99 % of messages delivered during the hold phase without subscriber
disconnect.

---

## P02 – QoS1 Message Rate Ramp

**Theme:** QoS1 publish-and-deliver throughput with PUBACK under increasing message rate.

**What it does:** Identical structure to P01 but QoS level 1. Measures the impact of per-message
ACKs on sustainable throughput. PUBACK latency (avg, p95) is reported as an additional counter.

**Parameters:**

```
# --- P02 parameters ---
PARAMS_P02 = {
    "small":  {"start_msg_s": 10,  "end_msg_s": 100,  "step": 10,  "ramp_interval_s": 3.0, "hold_seconds": 30},
    "middle": {"start_msg_s": 50,  "end_msg_s": 1000, "step": 50,  "ramp_interval_s": 2.0, "hold_seconds": 30},
    "large":  {"start_msg_s": 100, "end_msg_s": 8000, "step": 100, "ramp_interval_s": 1.5, "hold_seconds": 30},
}
```

**Success criteria:** ≥ 99 % of messages delivered and ≥ 99 % PUBACK received during hold phase.

---

## P03 – QoS2 Message Rate Ramp

**Theme:** QoS2 exactly-once throughput under increasing message rate (full 4-way handshake).

**What it does:** One publisher / one subscriber. Measures the broker's capacity to complete
PUBLISH → PUBREC → PUBREL → PUBCOMP cycles. Inflight-slot exhaustion (packet-id reuse pressure)
is observed by tracking pending vs. completed messages.

**Parameters:**

```
# --- P03 parameters ---
PARAMS_P03 = {
    "small":  {"start_msg_s": 5,  "end_msg_s": 50,  "step": 5,  "ramp_interval_s": 4.0, "hold_seconds": 30},
    "middle": {"start_msg_s": 20, "end_msg_s": 500, "step": 20, "ramp_interval_s": 2.0, "hold_seconds": 30},
    "large":  {"start_msg_s": 50, "end_msg_s": 3000,"step": 50, "ramp_interval_s": 1.5, "hold_seconds": 30},
}
```

**Success criteria:** ≥ 99 % of messages confirmed (PUBCOMP) and delivered during hold phase.

---

## P04 – Subscriber Fan-out Ramp

**Theme:** Fan-out delivery – one publisher, increasing number of subscribers.

**What it does:** A single publisher sends messages at a fixed rate. Subscribers are added one
by one during the ramp. At peak `end_subscribers`, the publisher continues for `hold_seconds`.
Total expected deliveries = messages_sent × subscriber_count.

**Parameters:**

```
# --- P04 parameters ---
PARAMS_P04 = {
    "small":  {"start_subscribers": 1, "end_subscribers": 10,  "step": 1,  "ramp_interval_s": 5.0,
               "msg_per_step": 20, "hold_seconds": 30, "qos": 0},
    "middle": {"start_subscribers": 1, "end_subscribers": 50,  "step": 5,  "ramp_interval_s": 3.0,
               "msg_per_step": 50, "hold_seconds": 30, "qos": 0},
    "large":  {"start_subscribers": 5, "end_subscribers": 200, "step": 10, "ramp_interval_s": 2.0,
               "msg_per_step": 100,"hold_seconds": 30, "qos": 0},
}
```

**Success criteria:** ≥ 99 % of expected deliveries (published × subscribers) received during
hold phase.

---

## P05 – Publisher Fan-in Ramp

**Theme:** Fan-in aggregation – increasing number of concurrent publishers, one subscriber.

**What it does:** One subscriber listens on a shared topic. Publishers are added step by step.
Each publisher sends at a fixed rate. At peak `end_publishers`, load is held. Tests the broker's
ability to merge concurrent inbound streams without dropping messages.

**Parameters:**

```
# --- P05 parameters ---
PARAMS_P05 = {
    "small":  {"start_publishers": 1, "end_publishers": 10,  "step": 1,  "ramp_interval_s": 5.0,
               "msg_per_publisher_per_step": 10, "hold_seconds": 30, "qos": 0},
    "middle": {"start_publishers": 1, "end_publishers": 50,  "step": 5,  "ramp_interval_s": 3.0,
               "msg_per_publisher_per_step": 20, "hold_seconds": 30, "qos": 0},
    "large":  {"start_publishers": 5, "end_publishers": 200, "step": 10, "ramp_interval_s": 2.0,
               "msg_per_publisher_per_step": 30, "hold_seconds": 30, "qos": 0},
}
```

**Success criteria:** ≥ 99 % of sent messages received by the single subscriber during hold.

---

## P06 – Large Payload Throughput Ramp

**Theme:** Message payload size – throughput degradation as payload grows.

**What it does:** Fixed single publisher / subscriber pair. Payload size increases each ramp
step. At peak `end_payload_bytes`, a burst of `hold_burst_count` messages is published and
reception is verified. Tests network buffer, serialization, and memory allocation under large
payloads.

**Parameters:**

```
# --- P06 parameters ---
PARAMS_P06 = {
    "small":  {"start_payload_bytes": 64,   "end_payload_bytes": 8192,  "step_bytes": 512,
               "ramp_interval_s": 2.0, "burst_count": 50, "hold_seconds": 20, "qos": 1},
    "middle": {"start_payload_bytes": 128,  "end_payload_bytes": 65536, "step_bytes": 4096,
               "ramp_interval_s": 2.0, "burst_count": 100,"hold_seconds": 20, "qos": 1},
    "large":  {"start_payload_bytes": 512,  "end_payload_bytes": 262144,"step_bytes": 16384,
               "ramp_interval_s": 2.0, "burst_count": 200,"hold_seconds": 20, "qos": 1},
}
```

**Success criteria:** All `burst_count` messages delivered at `end_payload_bytes` within
`hold_seconds`. Report bytes/s as throughput.

---

## P07 – Retained Message Replay Ramp

**Theme:** Retained message store – subscriber replay latency under increasing retained-topic
count.

**What it does:** A publisher stores retained messages across `end_topics` unique topics
(added ramp step by step). After each ramp step a fresh subscriber subscribes to the wildcard
`root/#` and drains the retained replay. Measures how quickly the broker delivers the full
retained set as topic count grows.

**Parameters:**

```
# --- P07 parameters ---
PARAMS_P07 = {
    "small":  {"start_topics": 10,  "end_topics": 200,  "step": 10,  "ramp_interval_s": 2.0,
               "hold_seconds": 20, "qos": 0},
    "middle": {"start_topics": 50,  "end_topics": 1000, "step": 50,  "ramp_interval_s": 2.0,
               "hold_seconds": 20, "qos": 0},
    "large":  {"start_topics": 200, "end_topics": 5000, "step": 200, "ramp_interval_s": 1.5,
               "hold_seconds": 20, "qos": 0},
}
```

**Success criteria:** ≥ 99 % of retained messages replayed to each new subscriber within the
configured timeout. Report replay latency (total time / retained count).

**Cleanup:** After test, publish zero-byte retained to all topics to clear the retained store.

---

## P08 – Offline Queue Pressure Ramp

**Theme:** Offline/persistent queue – QoS1 queue depth under increasing backlog then flush.

**What it does:** A subscriber connects with `clean_start=False` and `SessionExpiryInterval`
set, subscribes, then disconnects. A publisher enqueues an increasing number of QoS1 messages.
The subscriber reconnects and drains the queue. Ramp step = queued messages. Hold phase: queue
`end_queued` messages and drain immediately after reconnect.

**Parameters:**

```
# --- P08 parameters ---
PARAMS_P08 = {
    "small":  {"start_queued": 10,  "end_queued": 100,  "step": 10,  "ramp_interval_s": 3.0,
               "hold_seconds": 20, "session_expiry_s": 300, "drain_timeout_s": 30},
    "middle": {"start_queued": 50,  "end_queued": 500,  "step": 50,  "ramp_interval_s": 3.0,
               "hold_seconds": 20, "session_expiry_s": 300, "drain_timeout_s": 30},
    "large":  {"start_queued": 100, "end_queued": 2000, "step": 100, "ramp_interval_s": 2.0,
               "hold_seconds": 20, "session_expiry_s": 300, "drain_timeout_s": 60},
}
```

**Success criteria:** All queued messages delivered after reconnect in each ramp step. Zero
missing messages during hold phase.

---

## P09 – Subscription Churn Ramp

**Theme:** Subscription trie pressure – increasing subscribe/unsubscribe rate.

**What it does:** A pool of clients continuously cycles subscribe → unsubscribe on unique topic
filters. The subscribe rate (subscriptions per second) is ramped up by adding more clients.
During hold phase a publisher sends to all active filters; delivery ratio measures routing
correctness under high trie mutation load.

**Parameters:**

```
# --- P09 parameters ---
PARAMS_P09 = {
    "small":  {"start_clients": 2,  "end_clients": 10,  "step": 1,  "ramp_interval_s": 5.0,
               "filters_per_client": 10, "churn_interval_s": 0.5, "hold_seconds": 30},
    "middle": {"start_clients": 5,  "end_clients": 50,  "step": 5,  "ramp_interval_s": 3.0,
               "filters_per_client": 20, "churn_interval_s": 0.3, "hold_seconds": 30},
    "large":  {"start_clients": 10, "end_clients": 200, "step": 10, "ramp_interval_s": 2.0,
               "filters_per_client": 30, "churn_interval_s": 0.2, "hold_seconds": 30},
}
```

**Success criteria:** No broker errors during hold phase. Delivery ratio to stable (non-churning)
reference subscriber ≥ 99 %.

---

## P10 – Will Message Delivery Ramp

**Theme:** Will message delivery – broker reliably delivers WILL under increasing
abrupt-disconnect rate.

**What it does:** A set of "will clients" connect with a configured Will message, then are
abruptly closed (TCP disconnect, no DISCONNECT packet). A "will watcher" subscriber collects
delivered will messages. The abrupt-disconnect rate is ramped up. At hold phase the full set of
`end_clients` will clients disconnects simultaneously.

**Parameters:**

```
# --- P10 parameters ---
PARAMS_P10 = {
    "small":  {"start_clients": 2,  "end_clients": 20,  "step": 2,  "ramp_interval_s": 4.0,
               "hold_seconds": 30, "will_qos": 1, "will_delay_s": 0},
    "middle": {"start_clients": 5,  "end_clients": 100, "step": 5,  "ramp_interval_s": 3.0,
               "hold_seconds": 30, "will_qos": 1, "will_delay_s": 0},
    "large":  {"start_clients": 10, "end_clients": 400, "step": 10, "ramp_interval_s": 2.0,
               "hold_seconds": 30, "will_qos": 1, "will_delay_s": 0},
}
```

**Success criteria:** ≥ 99 % of expected will messages received by the watcher during hold phase.

---

## Duration extension

All scenarios accept a `--duration` argument (seconds, default 120). Any seconds beyond 120 are
added to `hold_seconds` only. The ramp time stays constant.

Formula:
```
effective_hold = hold_seconds + max(0, duration - 120)
```

---

## Implementation notes

1. **Parameter block** – Each `_scenario_pNN` function starts with a `PARAMS_PNN` dict (small /
   middle / large). All load values come from this dict; no magic numbers elsewhere in the
   function.
2. **Ramp helper** – A shared `_ramp_steps(start, end, step)` generator yields each load level
   with a `time.sleep(ramp_interval_s)` between steps. The hold phase is a final `time.sleep`.
3. **Scenario IDs** – P01–P10 with `stage_id` values 11–20 so they sort after S01–S10.
4. **ScenarioSpec entries** – added to `SCENARIO_SPECS` tuple in `run_performance_tests.py`.
5. **SIZE_STAGE_RANGES** – extended: `small` covers stages 1–13, `middle` 1–17, `large` 1–20.
6. **Throughput metric** – `work_units` = messages delivered (or bytes for P06) so the
   summary table shows a comparable msg/s (or bytes/s) column.
7. **Cleanup** – Each scenario disconnects all clients in a `finally` block. P07 clears
   retained messages after the test.
