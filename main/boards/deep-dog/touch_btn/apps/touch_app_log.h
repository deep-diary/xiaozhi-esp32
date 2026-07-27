#pragma once

#include "touch_btn/touch_app.h"

/** 联调用：打印所有触摸事件，不改驱动即可验证手势 */
class TouchAppLog : public ITouchApp {
public:
    const char* Name() const override { return "log"; }
    void OnEvent(const TouchEvent& ev) override;
};
