#pragma once

#include <cstddef>
#include <cstdint>

#include "face_ai_config.h"
#include "face_ai_types.h"

bool DeepDogFaceAiRuntimeStart();
void DeepDogFaceAiRuntimeStop();
void DeepDogFaceAiSetEnabled(bool on);
bool DeepDogFaceAiIsEnabled();
void DeepDogFaceAiSubmitFrameIfDue(const uint8_t* rgb565, size_t len, uint16_t width, uint16_t height);
size_t DeepDogFaceAiFormatJson(char* buf, size_t buf_size);
/** Immich 回写后刷新快照中的 display_name（若 primary 匹配）。 */
void DeepDogFaceAiOnImmichName(int local_id, const char* display_name);
/** 当前 primary local_id（无脸则为 0）。 */
int DeepDogFaceAiPrimaryLocalId();
/** 线程安全拷贝当前快照（MQTT 像素坐标用）。 */
void DeepDogFaceAiCopySnapshot(DeepDogFaceSnapshot* out);
