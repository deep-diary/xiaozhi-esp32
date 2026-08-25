#ifndef DEEP_DOG_MICROROS_CLIENT_H_
#define DEEP_DOG_MICROROS_CLIENT_H_

#include "config.h"

/**
 * CloudEdge CE01 micro-ROS Client。
 * 须在 STA 已获 IP 后调用 Start()；勿与 uros_network_interface_initialize 并用。
 */

#if DEEP_DOG_MICROROS_ENABLE

#ifdef __cplusplus
extern "C" {
#endif

/** 启动 micro-ROS 任务（幂等；已启动则忽略） */
void DeepDogMicrorosStart(void);

#ifdef __cplusplus
}
#endif

#else  // !DEEP_DOG_MICROROS_ENABLE

static inline void DeepDogMicrorosStart(void) {}

#endif  // DEEP_DOG_MICROROS_ENABLE

#endif  // DEEP_DOG_MICROROS_CLIENT_H_
