#!/bin/bash
# Deploys broker with tracing, runs P01 test, collects log after test ends.
set -e

TRACE_FILE="$(dirname "$0")/trace_output.jsonl"
REMOTE="pi@raspberrypi"
REMOTE_DIR="/home/pi/mqtt"

echo "=== Step 1: Deploy broker with tracing ===" | tee "$TRACE_FILE"
python3 "$(dirname "$0")/deploypi.py" \
  --remote-host "$REMOTE" \
  --remote-dir "$REMOTE_DIR" \
  --trace-level info \
  --trace-module executor \
  --trace-module connection \
  --show-log --log-lines 5

echo "=== Step 2: Run P01 performance test ===" | tee -a "$TRACE_FILE"
python3 "$(dirname "$0")/run_performance_tests.py" \
  --host raspberrypi \
  --size middle \
  --filter P01 2>&1 | tee -a "$TRACE_FILE" || true

echo "=== Step 3: Wait 10s for post-load CPU to show ===" | tee -a "$TRACE_FILE"
sleep 10

echo "=== Step 4: Collect broker log ===" | tee -a "$TRACE_FILE"
ssh "$REMOTE" "cat $REMOTE_DIR/broker.log 2>/dev/null || journalctl -u mqtt-broker --no-pager -n 500 2>/dev/null || true" >> "$TRACE_FILE"

echo "=== Done. Trace saved to: $TRACE_FILE ===" 
