#pragma once

#include <cstddef>
#include <cstdint>

#include "face_ai_config.h"

bool DeepDogFaceAiRuntimeStart();
void DeepDogFaceAiRuntimeStop();
void DeepDogFaceAiSetEnabled(bool on);
bool DeepDogFaceAiIsEnabled();
void DeepDogFaceAiSubmitFrameIfDue(const uint8_t* rgb565, size_t len, uint16_t width, uint16_t height);
size_t DeepDogFaceAiFormatJson(char* buf, size_t buf_size);
