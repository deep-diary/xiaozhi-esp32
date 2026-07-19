#pragma once

#include "face_ai_config.h"
#include "face_ai_types.h"

#include <list>
#include <vector>

#if DEEP_DOG_FACE_AI_ENABLE && DEEP_DOG_FACE_RECOG_ENABLE && defined(CONFIG_IDF_TARGET_ESP32S3)
#include "dl_detect_define.hpp"
#include "dl_image_define.hpp"
#endif

bool DeepDogFaceRecognizeInit();
void DeepDogFaceRecognizeDeinit();
bool DeepDogFaceRecognizeReady();

#if DEEP_DOG_FACE_AI_ENABLE && DEEP_DOG_FACE_RECOG_ENABLE && defined(CONFIG_IDF_TARGET_ESP32S3)
void DeepDogFaceRecognizeProcess(const dl::image::img_t& img, const std::list<dl::detect::result_t>& detect_raw,
                                 std::vector<DeepDogFaceBox>* boxes, DeepDogFaceSnapshot* snap_fields);
#endif
