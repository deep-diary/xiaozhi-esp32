# deep-dog CE01: on Windows, build libmicroros via WSL if missing (native make path breaks).
# Upstream: managed_components/micro-ros__micro_ros_espidf_component/CMakeLists.txt
if(CMAKE_HOST_WIN32)
    set(_UROS_LIB "${COMPONENT_DIR}/libmicroros.a")
    if(NOT EXISTS "${_UROS_LIB}")
        message(STATUS "[deep-dog] libmicroros.a missing; invoking WSL build script (first run is long)")
        find_program(DEEP_DOG_WSL wsl REQUIRED)
        execute_process(
            COMMAND ${DEEP_DOG_WSL} bash /mnt/d/working/dev/xiaozhi-esp32/scripts/deep_dog/wsl_build_libmicroros.sh
            RESULT_VARIABLE libmicroros_ret
        )
        if(libmicroros_ret AND NOT libmicroros_ret EQUAL 0)
            message(FATAL_ERROR
                "FAILED to build libmicroros via WSL (rc=${libmicroros_ret}). "
                "Or run: wsl bash scripts/deep_dog/wsl_build_libmicroros.sh")
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
endif()
