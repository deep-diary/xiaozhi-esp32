#pragma once

class LedStripControl;

/** 机器狗应用 ↔ LED 绑定（仅 mode=5 时生效；业务语义不上报 MQTT） */
void LedAppDogInit(LedStripControl* ctrl);
void LedAppDogOnEnterSystem(LedStripControl* ctrl);
