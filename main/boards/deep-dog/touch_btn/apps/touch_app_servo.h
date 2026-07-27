#pragma once

#include "touch_btn/touch_app.h"
#include "touch_btn/touch_config.h"

/**
 * 舵机调试触摸占位：SERVO 开启时注册。
 * 映射：短按1/2/3 微调提示；长按1 归中提示（实际 PWM 调用可后续接 Servo API）。
 */
class TouchAppServo : public ITouchApp {
public:
    const char* Name() const override { return "servo"; }
    void OnEvent(const TouchEvent& ev) override;
};
