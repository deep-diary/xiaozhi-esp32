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

/**
 * CAN 链路质量统计（联调诊断；上报见 status/can 的 rx_fps/rx_total/tx_total/bus_off_count/recover_count）。
 * 计数与「是否开 tunnel / 是否节流」无关，反映总线真实情况。
 */
typedef struct {
    uint32_t rx_frames;      /**< 累计从总线读到的帧数（含未被上报的） */
    uint32_t tx_frames;      /**< 累计写入总线的帧数（含网页注入） */
    uint32_t bus_off_events; /**< 累计进入 Bus-Off 次数 */
    uint32_t recover_count;  /**< 累计 Bus-Off 恢复成功（initiate_recovery 成功）次数 */
} DeepDogCanHubStats;

void DeepDogCanGetStats(DeepDogCanHubStats* out);
/** 由板级健康守护调用：检测到 Bus-Off / 恢复成功时各计一次 */
void DeepDogCanCountBusOff(void);
void DeepDogCanCountRecoverOk(void);

#ifdef __cplusplus
}
#endif

#endif
