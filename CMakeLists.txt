# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

# [deep-dog] 合入说明: Bluepad32 EXTRA_COMPONENT_DIRS 须在 project.cmake 之前设置
# 背景: 板载 Xbox BLE 主机依赖 Bluepad32/BTstack；默认关，见 handle/sources/README.md
option(DEEP_DOG_HANDLE_BT "deep-dog: enable Bluepad32+BTstack Xbox BLE host" OFF)
# [deep-dog] 合入说明: CloudEdge micro-ROS 需链接 micro_ros_espidf_component（默认关）
# 背景: libmicroros 首次编译很重；仅 CE01 联调时 -DDEEP_DOG_MICROROS=ON
option(DEEP_DOG_MICROROS "deep-dog: enable CloudEdge micro-ROS client (CE01)" OFF)
set(DEEP_DOG_BLUEPAD32_ROOT "${CMAKE_CURRENT_LIST_DIR}/main/boards/deep-dog/handle/third_party/bluepad32")
set(DEEP_DOG_MICROROS_COMP "${CMAKE_CURRENT_LIST_DIR}/managed_components/micro-ros__micro_ros_espidf_component")
if(DEEP_DOG_MICROROS)
    if(NOT EXISTS "${DEEP_DOG_MICROROS_COMP}/CMakeLists.txt")
        message(FATAL_ERROR
            "DEEP_DOG_MICROROS=ON but micro_ros_espidf_component missing at ${DEEP_DOG_MICROROS_COMP}. "
            "Run: idf.py add-dependency \"micro-ros/micro_ros_espidf_component>=22.0.0,<23.0.0\" "
            "(or scripts/deep_dog/deep_dog_fetch_microros.sh). See main/boards/deep-dog/microros/README.md")
    endif()
    # Keep component out of main/idf_component.yml so default builds skip libmicroros.
    # Component Manager still places it under managed_components after add-dependency.
    message(STATUS "[deep-dog] micro-ROS component at ${DEEP_DOG_MICROROS_COMP}")
endif()
if(DEEP_DOG_HANDLE_BT)
    if(EXISTS "${DEEP_DOG_BLUEPAD32_ROOT}/src/components/bluepad32/CMakeLists.txt")
        set(BLUEPAD32_ROOT "${DEEP_DOG_BLUEPAD32_ROOT}")
        set(BTSTACK_ROOT "${BLUEPAD32_ROOT}/external/btstack")
        # Must be set() (not only APPEND) before include(project.cmake)
        set(EXTRA_COMPONENT_DIRS "${BLUEPAD32_ROOT}/src/components")
        message(STATUS "[deep-dog] Bluepad32 EXTRA_COMPONENT_DIRS=${EXTRA_COMPONENT_DIRS}")
    else()
        message(FATAL_ERROR
            "DEEP_DOG_HANDLE_BT=ON but Bluepad32 missing at ${DEEP_DOG_BLUEPAD32_ROOT}. "
            "Run: ./scripts/deep_dog/deep_dog_fetch_bluepad32.sh")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
# [deep-dog] Bluepad32 / micro-ROS are dropped under MINIMAL_BUILD; disable trim when either is on.
if(DEEP_DOG_HANDLE_BT OR DEEP_DOG_MICROROS)
    idf_build_set_property(MINIMAL_BUILD OFF)
    message(STATUS "[deep-dog] MINIMAL_BUILD OFF (HANDLE_BT=${DEEP_DOG_HANDLE_BT} MICROROS=${DEEP_DOG_MICROROS})")
else()
    idf_build_set_property(MINIMAL_BUILD ON)
endif()

set(PROJECT_VER "2.3.0")
project(xiaozhi)
