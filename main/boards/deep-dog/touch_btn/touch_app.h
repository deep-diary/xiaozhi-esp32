#pragma once

#include "touch_btn/touch_event_hub.h"

/** 按键应用接口：驱动无关，由 TouchAppDispatcher 统一调度 */
class ITouchApp {
public:
    virtual ~ITouchApp() = default;
    virtual const char* Name() const = 0;
    virtual void OnEvent(const TouchEvent& ev) = 0;
};
