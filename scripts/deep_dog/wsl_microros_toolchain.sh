#!/usr/bin/env bash
set -euo pipefail
COMP=/mnt/d/working/dev/xiaozhi-esp32/managed_components/micro-ros__micro_ros_espidf_component
cd "$COMP"
echo "pwd=$(pwd)"
X_CC=$(ls /mnt/c/Users/gewei/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc.exe | head -1)
X_CXX=$(ls /mnt/c/Users/gewei/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++.exe | head -1)
X_AR=$(ls /mnt/c/Users/gewei/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp32s3-elf-ar.exe | head -1)
TARGET="$(pwd)/esp32_toolchain.cmake"
echo "TARGET=$TARGET"
make -f libmicroros.mk -j1 \
  "X_CC=${X_CC}" "X_CXX=${X_CXX}" "X_AR=${X_AR}" \
  C_STANDARD=17 MIDDLEWARE=microxrcedds \
  BUILD_DIR=/mnt/d/working/dev/xiaozhi-esp32/build \
  IDF_INCLUDES= \
  IDF_PATH=/mnt/d/working/dev/esp-idf \
  IDF_TARGET=esp32s3 \
  APP_COLCON_META=/mnt/d/working/dev/xiaozhi-esp32/app-colcon.meta \
  EXTRA_ROS_PACKAGES="${COMP}/extra_packages" \
  "$TARGET"
ls -la esp32_toolchain.cmake
