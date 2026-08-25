#!/usr/bin/env bash
set -euo pipefail
SRC=/mnt/d/working/dev/xiaozhi-esp32/managed_components/micro-ros__micro_ros_espidf_component/micro_ros_dev/src
DST=$HOME/uros_espidf_build/micro_ros_dev/src
mkdir -p "$DST"
python3 /mnt/d/working/dev/xiaozhi-esp32/scripts/deep_dog/wsl_copy_ament_lf.py
cd "$HOME/uros_espidf_build/micro_ros_dev"
rm -rf build install log
export PATH="$HOME/.local/bin:$PATH"
colcon build --packages-up-to ament_cmake --cmake-args -DBUILD_TESTING=OFF
file install/ament_cmake_core/share/ament_cmake_core/environment/path.sh
test -d install/ament_cmake && echo AMENT_OK
