#pragma once

#include "handle/handle_app.h"
#include "handle/handle_config.h"
#include "handle/handle_event_hub.h"

#include <esp_timer.h>

class HandleAppDispatcher {
public:
    static constexpr int kMaxApps = 8;

    explicit HandleAppDispatcher(HandleEventHub* hub);
    ~HandleAppDispatcher();

    bool Register(IHandleApp* app);
    bool StartPeriodic(uint64_t interval_us);
    void Stop();
    void Poll();

private:
    static void TimerCb(void* arg);

    HandleEventHub* hub_ = nullptr;
    IHandleApp* apps_[kMaxApps] = {};
    int app_count_ = 0;
    esp_timer_handle_t timer_ = nullptr;
};
