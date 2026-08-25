#!/usr/bin/env bash
# Fetch micro-ROS ESP-IDF component (Humble / major 22) for deep-dog CE01.
# Run from repo root with IDF env loaded. Do NOT source ROS 2 setup.
set -euo pipefail
idf.py add-dependency "micro-ros/micro_ros_espidf_component>=22.0.0,<23.0.0"
echo "Fetched. Windows: python scripts/deep_dog/install_microros_prebuilt.py"
echo "       then:     python scripts/deep_dog/patch_microros_windows_cmake.py"
echo "Then: idf.py -DDEEP_DOG_MICROROS=ON build"
