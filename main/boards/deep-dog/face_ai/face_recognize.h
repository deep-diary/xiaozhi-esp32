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

/** S05：该 local_id 是否仍需 Immich 真名（无 person_id 或 display 仍为 #n）。 */
bool DeepDogFaceRecognizeNeedsImmichName(int local_id);
/** S05：写入真名与 Immich person_id 到 NVS meta。 */
bool DeepDogFaceRecognizeBindImmichName(int local_id, const char* display_name, const char* immich_person_id);

#if DEEP_DOG_FACE_AI_ENABLE && DEEP_DOG_FACE_RECOG_ENABLE && defined(CONFIG_IDF_TARGET_ESP32S3)
void DeepDogFaceRecognizeProcess(const dl::image::img_t& img, const std::list<dl::detect::result_t>& detect_raw,
                                 std::vector<DeepDogFaceBox>* boxes, DeepDogFaceSnapshot* snap_fields);
#endif
