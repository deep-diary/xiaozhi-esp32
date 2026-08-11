#pragma once

/** RTSP 推流与语音互斥（V-S08）：推流时暂停 AFE，停流后恢复待机唤醒。 */

void DeepDogStreamAudioGateSetRtspActive(bool active);
bool DeepDogStreamAudioGateIsVoicePaused();
