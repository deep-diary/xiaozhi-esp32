#!/usr/bin/env bash
# Fetch Bluepad32 + patch/integrate BTstack for deep-dog handle BT source.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${ROOT}/main/boards/deep-dog/handle/third_party/bluepad32"
REPO_URL="${DEEP_DOG_BLUEPAD32_URL:-https://github.com/ricardoquesada/bluepad32.git}"
REF="${DEEP_DOG_BLUEPAD32_REF:-main}"

mkdir -p "$(dirname "$DEST")"

if [[ -d "${DEST}/.git" ]]; then
  echo "Updating existing clone at ${DEST}"
  git -C "${DEST}" fetch --recurse-submodules origin
  git -C "${DEST}" checkout "${REF}"
  git -C "${DEST}" pull --ff-only origin "${REF}" || true
  git -C "${DEST}" submodule update --init --recursive
else
  echo "Cloning ${REPO_URL} (${REF}) → ${DEST}"
  rm -rf "${DEST}"
  git clone --recursive --branch "${REF}" "${REPO_URL}" "${DEST}"
fi

echo "Applying BTstack patches..."
(
  cd "${DEST}/external/btstack"
  shopt -s nullglob
  for p in ../patches/*.patch; do
    echo "  git apply ${p}"
    git apply "${p}" || git apply --reverse --check "${p}" 2>/dev/null || true
  done
)

echo "Integrating BTstack into Bluepad32 src/components..."
(
  cd "${DEST}/external/btstack/port/esp32"
  # Installs btstack component under bluepad32/src/components (gitignored there)
  IDF_PATH="${DEST}/src" ./integrate_btstack.py
)

echo "Done. Build with: idf.py -DDEEP_DOG_HANDLE_BT=ON reconfigure build"
echo "See: main/boards/deep-dog/handle/sources/README.md"
