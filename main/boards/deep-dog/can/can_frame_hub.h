#pragma once

#include "config.h"

#if DEEP_DOG_CAN_ENABLE

#include "can/ESP32-TWAI-CAN.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*DeepDogCanFrameListener)(const CanFrame* frame, int is_tx, void* ctx);

void DeepDogCanSetFrameListener(DeepDogCanFrameListener cb, void* ctx);
void DeepDogCanNotifyFrame(const CanFrame* frame, int is_tx);

#ifdef __cplusplus
}
#endif

#endif
