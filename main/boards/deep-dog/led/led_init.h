#pragma once

class LedStripControl;
class CircularStrip;

/** 灯带初始化：ENABLE 且 GPIO/count 有效时创建 CircularStrip + LedStripControl */
void DeepDogLedInit(void);

LedStripControl* DeepDogLedGetControl(void);
CircularStrip* DeepDogLedGetStrip(void);
