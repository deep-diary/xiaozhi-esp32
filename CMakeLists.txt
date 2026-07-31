# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

# [deep-dog] 合入说明: Bluepad32 EXTRA_COMPONENT_DIRS 须在 project.cmake 之前设置
# 背景: 板载 Xbox BLE 主机依赖 Bluepad32/BTstack；默认关，见 handle/sources/README.md
option(DEEP_DOG_HANDLE_BT "deep-dog: enable Bluepad32+BTstack Xbox BLE host" OFF)
set(DEEP_DOG_BLUEPAD32_ROOT "${CMAKE_SOURCE_DIR}/main/boards/deep-dog/handle/third_party/bluepad32")
if(DEEP_DOG_HANDLE_BT)
    if(EXISTS "${DEEP_DOG_BLUEPAD32_ROOT}/src/components/bluepad32")
        set(BLUEPAD32_ROOT "${DEEP_DOG_BLUEPAD32_ROOT}")
        set(BTSTACK_ROOT "${BLUEPAD32_ROOT}/external/btstack")
        list(APPEND EXTRA_COMPONENT_DIRS "${BLUEPAD32_ROOT}/src/components")
    else()
        message(FATAL_ERROR
            "DEEP_DOG_HANDLE_BT=ON but Bluepad32 missing at ${DEEP_DOG_BLUEPAD32_ROOT}. "
            "Run: ./scripts/deep_dog_fetch_bluepad32.sh")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

set(PROJECT_VER "2.3.0")
project(xiaozhi)
