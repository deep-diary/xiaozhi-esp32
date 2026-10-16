#include "can/can_frame_hub.h"

#if DEEP_DOG_CAN_ENABLE

namespace {
DeepDogCanFrameListener s_cb = nullptr;
void* s_ctx = nullptr;
uint32_t s_rx_frames = 0;
uint32_t s_tx_frames = 0;
uint32_t s_bus_off_events = 0;
uint32_t s_recover_count = 0;
}

void DeepDogCanSetFrameListener(DeepDogCanFrameListener cb, void* ctx) {
    s_cb = cb;
    s_ctx = ctx;
}

void DeepDogCanNotifyFrame(const CanFrame* frame, int is_tx) {
    if (!frame) {
        return;
    }
    if (is_tx) {
        __atomic_fetch_add(&s_tx_frames, 1, __ATOMIC_RELAXED);
    } else {
        __atomic_fetch_add(&s_rx_frames, 1, __ATOMIC_RELAXED);
    }
    if (s_cb) {
        s_cb(frame, is_tx, s_ctx);
    }
}

void DeepDogCanCountBusOff(void) {
    __atomic_fetch_add(&s_bus_off_events, 1, __ATOMIC_RELAXED);
}

void DeepDogCanCountRecoverOk(void) {
    __atomic_fetch_add(&s_recover_count, 1, __ATOMIC_RELAXED);
}

void DeepDogCanGetStats(DeepDogCanHubStats* out) {
    if (!out) {
        return;
    }
    out->rx_frames = __atomic_load_n(&s_rx_frames, __ATOMIC_RELAXED);
    out->tx_frames = __atomic_load_n(&s_tx_frames, __ATOMIC_RELAXED);
    out->bus_off_events = __atomic_load_n(&s_bus_off_events, __ATOMIC_RELAXED);
    out->recover_count = __atomic_load_n(&s_recover_count, __ATOMIC_RELAXED);
}

#endif
