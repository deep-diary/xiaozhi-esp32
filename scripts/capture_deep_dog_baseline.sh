#!/usr/bin/env bash
# Flash deep-dog firmware and capture serial boot log for baseline comparison.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

DURATION="${1:-90}"
MAX_WAIT="${2:-600}"
PORT="${ESP_PORT:-}"

export IDF_PATH="${IDF_PATH:-/Volumes/MacExtStorage/projects/esp-idf-v5.5.2}"
export IDF_PYTHON_ENV_PATH="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/python_env/idf5.5_py3.11_env}"
# shellcheck disable=SC1090
. "$IDF_PATH/export.sh"

if [[ -z "$PORT" ]]; then
  echo "Waiting up to ${MAX_WAIT}s for /dev/cu.usbmodem* ..."
  elapsed=0
  while [[ $elapsed -lt $MAX_WAIT ]]; do
    PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
    if [[ -n "$PORT" ]]; then
      break
    fi
    sleep 5
    elapsed=$((elapsed + 5))
  done
fi

if [[ -z "$PORT" ]]; then
  echo "ERROR: No ESP USB serial port found." >&2
  exit 1
fi

echo "Using port: $PORT"

VER="$(grep -m1 'set(PROJECT_VER' CMakeLists.txt | sed 's/.*"\([^"]*\)".*/\1/')"
COMMIT="$(git rev-parse --short HEAD)"
DATE="$(date +%Y%m%d)"
LOG_DIR="main/boards/deep-dog/baseline-logs"
LOG_FILE="${LOG_DIR}/deep-dog-v${VER}-${COMMIT}-boot-${DATE}.log"
mkdir -p "$LOG_DIR"

echo "Flashing deep-dog ..."
idf.py -p "$PORT" flash

echo "Capturing ${DURATION}s serial log -> ${LOG_FILE}"
python3 << PYEOF
import serial
import time
import sys
from datetime import datetime

port = "${PORT}"
baud = 115200
duration = int("${DURATION}")
log_path = "${LOG_FILE}"
ver = "${VER}"
commit = "${COMMIT}"

header = f"""# deep-dog baseline boot log
# captured_at: {datetime.now().isoformat(timespec='seconds')}
# board: deep-dog
# firmware: xiaozhi v{ver} (git {commit} on $(git branch --show-current))
# target: esp32s3, OV2640 RGB565 240x240@25fps
# port: {port} @ {baud}
# duration_seconds: {duration}
# ---
"""

chunks = [header]
ser = serial.Serial(port, baud, timeout=0.2)
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

ser.close()
with open(log_path, "w", encoding="utf-8") as f:
    f.write("".join(chunks))
print(f"\n--- saved {len(''.join(chunks))} bytes to {log_path} ---", file=sys.stderr)
PYEOF

echo "Done: $LOG_FILE"
