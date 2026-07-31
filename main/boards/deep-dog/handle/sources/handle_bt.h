#pragma once

#include "handle/handle_config.h"
#include "handle/handle_types.h"

#include <cstdint>

class HandleEventHub;

/**
 * 板载 Bluepad32 + BTstack（Xbox BLE）适配。
 * DEEP_DOG_HANDLE_BT_ENABLE=0 时为桩；=1 时需 third_party/bluepad32（见 README）。
 */
bool HandleBtStart(HandleEventHub* hub);
void HandleBtStop();

/** 启动扫描 / 自动连接（handle/cmd pair） */
void HandleBtStartPairing();

/** 双马达震动；可从非 BTstack 线程调用 */
void HandleBtRumble(uint16_t delay_ms, uint16_t duration_ms, uint8_t weak, uint8_t strong);

bool HandleBtIsReady();
