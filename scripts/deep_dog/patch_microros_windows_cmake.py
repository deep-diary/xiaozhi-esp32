#!/usr/bin/env python3
"""Patch micro-ROS ESP-IDF component CMakeLists for deep-dog CE01.

1) Skip libmicroros when DEEP_DOG_MICROROS=OFF (component may still sit in managed_components).
2) On Windows, build libmicroros via WSL script when ON and .a missing.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
cmake = ROOT / "managed_components/micro-ros__micro_ros_espidf_component/CMakeLists.txt"
if not cmake.is_file():
    raise SystemExit(f"missing {cmake} — run scripts/deep_dog/deep_dog_fetch_microros.sh first")

text = cmake.read_text(encoding="utf-8")

skip_marker = "[deep-dog] skip libmicroros when DEEP_DOG_MICROROS=OFF"
if skip_marker not in text:
    needle = "idf_component_register(SRCS ${COMPONENT_SRC}\n                       INCLUDE_DIRS \"network_interfaces\"\n                       REQUIRES nvs_flash esp_wifi esp_eth lwip)\n"
    insert = needle + f"""
# {skip_marker}
if(NOT DEEP_DOG_MICROROS)
    message(STATUS "[deep-dog] micro-ROS component idle (DEEP_DOG_MICROROS=OFF)")
    return()
endif()
"""
    if needle not in text:
        raise SystemExit("idf_component_register block not found")
    text = text.replace(needle, insert, 1)

old_exec = """execute_process(
    WORKING_DIRECTORY ${COMPONENT_DIR}
    RESULT_VARIABLE libmicroros_ret
    COMMAND
        ${submake} -j -f libmicroros.mk
                X_CC=${CMAKE_C_COMPILER}
                X_AR=${CMAKE_AR}
                X_CXX=${CMAKE_CXX_COMPILER}
                C_STANDARD=${CMAKE_C_STANDARD}
                MIDDLEWARE=${MIDDLEWARE}
                BUILD_DIR=${CMAKE_BINARY_DIR}
                IDF_INCLUDES=${IDF_INCLUDES}
                IDF_PATH=${IDF_PATH}
                IDF_TARGET=${IDF_TARGET}
                APP_COLCON_META=${APP_COLCON_META}
                EXTRA_ROS_PACKAGES=${EXTRA_ROS_PACKAGES}
)
if(libmicroros_ret AND NOT libmicroros_ret EQUAL 0)
  message(FATAL_ERROR "FAILED: ${libmicroros_ret}")
endif()"""

new_exec = """# [deep-dog] Windows: build libmicroros via WSL (native Gnu make path breaks).
# See scripts/deep_dog/wsl_build_libmicroros.sh — re-run this patch after component manager update.
if(WIN32)
    set(_UROS_LIB "${COMPONENT_DIR}/libmicroros.a")
    if(NOT EXISTS "${_UROS_LIB}")
        message(STATUS "[deep-dog] libmicroros.a missing; invoking WSL build (first run is long)")
        find_program(DEEP_DOG_WSL wsl REQUIRED)
        file(TO_CMAKE_PATH "${PROJECT_DIR}" _pd)
        string(REGEX REPLACE "^([A-Za-z]):" "/mnt/\\\\1" _wsl_root "${_pd}")
        string(SUBSTRING "${_wsl_root}" 0 6 _pref)
        string(SUBSTRING "${_wsl_root}" 6 -1 _rest)
        string(TOLOWER "${_pref}" _pref)
        set(_wsl_root "${_pref}${_rest}")
        execute_process(
            COMMAND ${DEEP_DOG_WSL} env MICROROS_PROJECT_ROOT=${_wsl_root}
                    bash ${_wsl_root}/scripts/deep_dog/wsl_build_libmicroros.sh
            RESULT_VARIABLE libmicroros_ret
        )
        if(libmicroros_ret AND NOT libmicroros_ret EQUAL 0)
            message(FATAL_ERROR
                "FAILED WSL libmicroros rc=${libmicroros_ret}. "
                "Run: wsl bash scripts/deep_dog/wsl_build_libmicroros.sh")
        endif()
    else()
        message(STATUS "[deep-dog] using existing libmicroros.a")
    endif()
else()
    execute_process(
        WORKING_DIRECTORY ${COMPONENT_DIR}
        RESULT_VARIABLE libmicroros_ret
        COMMAND
            ${submake} -j -f libmicroros.mk
                    X_CC=${CMAKE_C_COMPILER}
                    X_AR=${CMAKE_AR}
                    X_CXX=${CMAKE_CXX_COMPILER}
                    C_STANDARD=${CMAKE_C_STANDARD}
                    MIDDLEWARE=${MIDDLEWARE}
                    BUILD_DIR=${CMAKE_BINARY_DIR}
                    IDF_INCLUDES=${IDF_INCLUDES}
                    IDF_PATH=${IDF_PATH}
                    IDF_TARGET=${IDF_TARGET}
                    APP_COLCON_META=${APP_COLCON_META}
                    EXTRA_ROS_PACKAGES=${EXTRA_ROS_PACKAGES}
    )
    if(libmicroros_ret AND NOT libmicroros_ret EQUAL 0)
      message(FATAL_ERROR "FAILED: ${libmicroros_ret}")
    endif()
endif()"""

if "[deep-dog] Windows: build libmicroros via WSL" not in text:
    if old_exec not in text:
        # already patched to win32 block or upstream changed
        if "FAILED WSL libmicroros" not in text and old_exec not in text:
            print("warning: execute_process block not found; skip/windows patch may be partial")
    else:
        text = text.replace(old_exec, new_exec, 1)

cmake.write_text(text, encoding="utf-8")
print(f"patched {cmake}")
