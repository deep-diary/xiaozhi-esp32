#include "can/can_frame_hub.h"

#if DEEP_DOG_CAN_ENABLE

namespace {
DeepDogCanFrameListener s_cb = nullptr;
void* s_ctx = nullptr;
}

void DeepDogCanSetFrameListener(DeepDogCanFrameListener cb, void* ctx) {
    s_cb = cb;
    s_ctx = ctx;
}

void DeepDogCanNotifyFrame(const CanFrame* frame, int is_tx) {
    if (s_cb && frame) {
        s_cb(frame, is_tx, s_ctx);
    }
}

#endif
