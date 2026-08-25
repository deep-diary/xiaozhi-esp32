#!/usr/bin/env bash
# Build micro-ROS libmicroros.a for deep-dog CE01 on Windows via WSL.
# Usage: wsl env MICROROS_PROJECT_ROOT=/mnt/d/working/dev/xiaozhi-esp32 \
#          bash scripts/deep_dog/wsl_build_libmicroros.sh
set -euo pipefail

ROOT="${MICROROS_PROJECT_ROOT:-/mnt/d/working/dev/xiaozhi-esp32}"
COMP="${ROOT}/managed_components/micro-ros__micro_ros_espidf_component"
IDF_PATH="${MICROROS_IDF_PATH:-/mnt/d/working/dev/esp-idf}"
BUILD_DIR="${ROOT}/build"
TOOL_GCC_GLOB="${MICROROS_XTENSA_GCC_GLOB:-/mnt/c/Users/gewei/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc.exe}"

if [[ ! -d "$COMP" ]]; then
  echo "missing component at $COMP — run idf.py with -DDEEP_DOG_MICROROS=ON once to fetch it" >&2
  exit 1
fi

cd "$COMP"

X_CC=$(ls $TOOL_GCC_GLOB | head -1)
X_CXX=${X_CC/gcc.exe/g++.exe}
X_AR=${X_CC/gcc.exe/ar.exe}

python3 -m pip install -q --user catkin_pkg colcon-common-extensions lark 'empy==3.3.4' 2>/dev/null || true
export PATH="${HOME}/.local/bin:${PATH}"
# Do not source ROS 2 setup (contaminates IDF / colcon cross build)

echo "[wsl_microros] ROOT=$ROOT"
echo "[wsl_microros] CC=$X_CC"
echo "[wsl_microros] building libmicroros.a (long first run)..."
make -f libmicroros.mk -j"$(nproc 2>/dev/null || echo 4)" \
  "X_CC=${X_CC}" "X_CXX=${X_CXX}" "X_AR=${X_AR}" \
  C_STANDARD=17 MIDDLEWARE=microxrcedds \
  BUILD_DIR="${BUILD_DIR}" \
  IDF_INCLUDES= \
  IDF_PATH="${IDF_PATH}" \
  IDF_TARGET=esp32s3 \
  APP_COLCON_META="${ROOT}/app-colcon.meta" \
  EXTRA_ROS_PACKAGES="${COMP}/extra_packages"

ls -la "${COMP}/libmicroros.a"
echo "[wsl_microros] done"
