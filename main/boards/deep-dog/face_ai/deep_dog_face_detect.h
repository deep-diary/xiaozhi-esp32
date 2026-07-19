#pragma once

#include "face_ai_config.h"
#include "face_ai_types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#if DEEP_DOG_FACE_AI_ENABLE && defined(CONFIG_IDF_TARGET_ESP32S3)
#include <list>

#include "dl_detect_define.hpp"
#include "dl_image_define.hpp"
#endif

bool DeepDogFaceDetectInit();
void DeepDogFaceDetectDeinit();

#if DEEP_DOG_FACE_AI_ENABLE && defined(CONFIG_IDF_TARGET_ESP32S3)
/**
 * 检测；若 raw_out 非空则填入同一次 run 的原始结果（含 keypoints），供识别复用。
 */
bool DeepDogFaceDetectRun(const uint8_t* rgb565, size_t len, uint16_t w, uint16_t h,
                          std::vector<DeepDogFaceBox>* out, std::list<dl::detect::result_t>* raw_out = nullptr);

/** 构造与 DetectRun 相同的 img_t；owned_buf 非空时调用方须 heap_caps_free */
bool DeepDogFaceDetectMakeImg(const uint8_t* rgb565, size_t len, uint16_t w, uint16_t h, dl::image::img_t* img,
                              uint8_t** owned_buf);
#else
bool DeepDogFaceDetectRun(const uint8_t* rgb565, size_t len, uint16_t w, uint16_t h, std::vector<DeepDogFaceBox>* out);
#endif
