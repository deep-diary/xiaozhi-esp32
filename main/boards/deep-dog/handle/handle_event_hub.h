#pragma once

#include "handle/handle_config.h"
#include "handle/handle_types.h"

#include <cstdint>
#include <functional>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/portmacro.h>

/**
 * 手柄快照 Hub：Push 更新最新态；队列供 Dispatcher；listener 供 MQTT。
 * apps_enabled_ 为 false 时仍更新快照/MQTT，App 自行跳过执行器。
 */
class HandleEventHub {
public:
    using PushListener = std::function<void(const HandleSnapshot&)>;

    HandleEventHub();
    ~HandleEventHub();

    HandleEventHub(const HandleEventHub&) = delete;
    HandleEventHub& operator=(const HandleEventHub&) = delete;

    bool Init(UBaseType_t depth = DEEP_DOG_HANDLE_EVENT_QUEUE_DEPTH);

    bool Push(const HandleSnapshot& snap);
    bool Pop(HandleSnapshot* out, TickType_t wait = 0);

    void SetPushListener(PushListener listener);

    HandleSnapshot GetSnapshot() const;

    void SetAppsEnabled(bool enabled);
    bool AppsEnabled() const;

private:
    QueueHandle_t queue_ = nullptr;
    PushListener listener_;
    HandleSnapshot snapshot_{};
    bool apps_enabled_ = true;
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};
