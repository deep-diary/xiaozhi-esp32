#!/usr/bin/env bash
# E2E: 模拟见到葛维冬并触发 face_greet 唤醒链
set -euo pipefail

DEV="${1:-1051db848fd4}"
NAME="${2:-葛维冬}"
BROKER="${3:-127.0.0.1}"
PREFIX="deepdiary/deep-dog/${DEV}"

echo "== simulate_greet name=${NAME} device=${DEV} broker=${BROKER} =="
mosquitto_pub -h "$BROKER" -t "${PREFIX}/face/cmd" \
  -m "{\"action\":\"simulate_greet\",\"name\":\"${NAME}\",\"local_id\":12}"

sleep 2
echo "== device/status (one message) =="
mosquitto_sub -h "$BROKER" -t "${PREFIX}/device/status" -C 1 -W 8 || true

echo "== person/active (retain) =="
mosquitto_sub -h "$BROKER" -t "${PREFIX}/person/active" -C 1 -W 3 || true

read -r -p "Press enter to clear_speaker..." _
mosquitto_pub -h "$BROKER" -t "${PREFIX}/face/cmd" -m '{"action":"clear_speaker"}'
echo "done"
