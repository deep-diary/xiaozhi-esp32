#!/usr/bin/env bash
# Build/flash deep-dog, capture serial, exercise face/cmd via MQTT, analyze log.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

DURATION="${1:-150}"
PORT="${ESP_PORT:-/dev/cu.usbmodem1101}"
BROKER="${MQTT_BROKER:-192.168.31.25}"
DEVICE_ID="${MQTT_DEVICE_ID:-1051db847e88}"
PREFIX="deepdiary/deep-dog/${DEVICE_ID}"

export IDF_PATH="${IDF_PATH:-/Volumes/MacExtStorage/projects/esp-idf-v5.5.2}"
export IDF_PYTHON_ENV_PATH="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/python_env/idf5.5_py3.11_env}"
# shellcheck disable=SC1090
. "$IDF_PATH/export.sh"

VER="$(grep -m1 'set(PROJECT_VER' CMakeLists.txt | sed 's/.*"\([^"]*\)".*/\1/')"
COMMIT="$(git rev-parse --short HEAD)"
DATE="$(date +%Y%m%d)"
LOG_DIR="main/boards/deep-dog/baseline-logs"
LOG_FILE="${LOG_DIR}/deep-dog-v${VER}-${COMMIT}-test-${DATE}.log"
mkdir -p "$LOG_DIR"

echo "=== Build ==="
if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
  idf.py -DBOARD_NAME=deep-dog -DBOARD_TYPE=deep-dog build
else
  echo "(SKIP_BUILD=1)"
fi

echo "=== Flash $PORT ==="
idf.py -p "$PORT" flash

echo "=== Serial capture ${DURATION}s + MQTT tests -> $LOG_FILE ==="
python3 << PYEOF
import json
import re
import serial
import sys
import threading
import time
from datetime import datetime

import paho.mqtt.client as mqtt

port = "${PORT}"
duration = int("${DURATION}")
log_path = "${LOG_FILE}"
broker = "${BROKER}"
prefix = "${PREFIX}"
ver = "${VER}"
commit = "${COMMIT}"

header = f"""# deep-dog face integration test log
# captured_at: {datetime.now().isoformat(timespec='seconds')}
# board: deep-dog
# firmware: xiaozhi v{ver} (git {commit})
# port: {port}
# mqtt: {broker} prefix={prefix}
# duration_seconds: {duration}
# ---
"""

chunks = [header]
registry_payload = {"raw": None, "parsed": None, "after_clear": None}
mqtt_done = {"clear_db": False, "registry_ok": False, "device_ready": False}

def on_connect(client, userdata, flags, reason_code, properties=None):
    client.subscribe(f"{prefix}/face/registry", qos=0)

def maybe_send_cmds(client):
    if not mqtt_done["device_ready"]:
        return
    if mqtt_done.get("cmds_sent"):
        return
    mqtt_done["cmds_sent"] = True
    client.publish(f"{prefix}/face/cmd", json.dumps({"action": "clear_db", "ts": int(time.time())}), qos=1)
    time.sleep(8)
    client.publish(f"{prefix}/face/cmd", json.dumps({"action": "ping_immich", "ts": int(time.time())}), qos=1)
    time.sleep(2)
    client.publish(f"{prefix}/face/cmd", json.dumps({"action": "refresh_status", "ts": int(time.time())}), qos=1)

def on_message(client, userdata, msg):
    if msg.topic.endswith("/face/registry"):
        try:
            text = msg.payload.decode("utf-8", errors="replace")
            registry_payload["raw"] = text
            registry_payload["parsed"] = json.loads(text)
            p = registry_payload["parsed"]
            if isinstance(p, dict) and "clock_synced" in p and "ts_iso" in p:
                mqtt_done["registry_ok"] = True
            if mqtt_done["clear_db"] or (isinstance(p, dict) and p.get("count") == 0):
                registry_payload["after_clear"] = registry_payload["parsed"]
        except Exception as e:
            print(f"registry parse err: {e}", file=sys.stderr)

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_message = on_message
try:
    client.connect(broker, 1883, 60)
except Exception as e:
    print(f"MQTT connect failed: {e}", file=sys.stderr)
    client = None

if client:
    client.loop_start()

# Fallback: if serial never signals ready within 45s, send anyway
def fallback_sender():
    time.sleep(45)
    if client and not mqtt_done.get("cmds_sent"):
        mqtt_done["device_ready"] = True
        maybe_send_cmds(client)

threading.Thread(target=fallback_sender, daemon=True).start()

ser = serial.Serial(port, 115200, timeout=0.2)
ser.dtr = False
time.sleep(0.05)
ser.dtr = True
time.sleep(0.05)
ser.dtr = False
time.sleep(0.2)
ser.reset_input_buffer()

end = time.time() + duration
while time.time() < end:
    data = ser.read(8192)
    if data:
        text = data.decode("utf-8", errors="replace")
        sys.stdout.write(text)
        chunks.append(text)
        if "dog_face_rec: recognizer ready" in text:
            mqtt_done["device_ready"] = True
            if client:
                maybe_send_cmds(client)
        elif "board MQTT started" in text and not mqtt_done.get("cmds_sent"):
            pass  # wait for recognizer before clear_db
        if "face/cmd clear_db ok=1" in text or "clear_db ok" in text:
            mqtt_done["clear_db"] = True

ser.close()
if client:
    client.loop_stop()
    client.disconnect()

body = "".join(chunks)
with open(log_path, "w", encoding="utf-8") as f:
    f.write(body)

checks = {
    "ws_mcp_ok": "dog_ws_mcp: WS MCP bridge" in body and "httpd_start failed" not in body,
    "face_ready": "memory report [face_ready]" in body,
    "clock_synced": "clock synced: republished" in body,
    "immich_worker": "immich worker ready" in body or "immich worker started" in body,
    "immich_late_fail": "immich_late task create failed" in body,
    "clear_db_ok": mqtt_done["clear_db"] or "clear_db ok" in body,
    "clear_db_fail": "face/cmd clear_db ok=0" in body or "clear_db: facedb remount failed" in body,
    "registry_fields": mqtt_done["registry_ok"],
    "registry_empty_after_clear": (
        registry_payload["after_clear"] is not None and registry_payload["after_clear"].get("count") == 0
    ),
    "enroll_fail": "enroll failed" in body or "enroll_feat failed" in body,
    "panic": "Guru Meditation" in body or "abort()" in body,
}

print("\n=== ANALYSIS ===", file=sys.stderr)
for k, v in checks.items():
    print(f"  {k}: {v}", file=sys.stderr)

if registry_payload["parsed"]:
    p = registry_payload["parsed"]
    print(f"  registry.count: {p.get('count')}", file=sys.stderr)
    print(f"  registry.clock_synced: {p.get('clock_synced')}", file=sys.stderr)
    print(f"  registry.ts_iso: {p.get('ts_iso')}", file=sys.stderr)
    entries = p.get("entries") or []
    if entries:
        e0 = entries[0]
        print(f"  registry.entry0.last_seen_iso: {e0.get('last_seen_iso', '(none)')}", file=sys.stderr)
else:
    print("  registry: (not received via MQTT)", file=sys.stderr)

fail = []
if not checks["ws_mcp_ok"]:
    fail.append("ws_mcp")
if checks["immich_late_fail"] and not checks["immich_worker"]:
    fail.append("immich_worker")
if checks["clear_db_fail"] or not checks["clear_db_ok"]:
    fail.append("clear_db")
if not checks["registry_fields"]:
    fail.append("registry_fields")
if checks["clear_db_ok"] and not checks.get("registry_empty_after_clear", False):
    fail.append("registry_not_empty_after_clear")
if checks["panic"]:
    fail.append("panic")

print(f"FAILURES: {fail or 'none'}", file=sys.stderr)
print(f"Saved: {log_path}", file=sys.stderr)
sys.exit(1 if fail else 0)
PYEOF
