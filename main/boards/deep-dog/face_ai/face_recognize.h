#pragma once

#include "face_ai_config.h"
#include "face_ai_types.h"

#include <cstddef>
#include <functional>
#include <list>
#include <vector>

#if DEEP_DOG_FACE_AI_ENABLE && DEEP_DOG_FACE_RECOG_ENABLE && defined(CONFIG_IDF_TARGET_ESP32S3)
#include "dl_detect_define.hpp"
#include "dl_image_define.hpp"
#endif

bool DeepDogFaceRecognizeInit();
void DeepDogFaceRecognizeDeinit();
bool DeepDogFaceRecognizeReady();
bool DeepDogFaceRecognizeClearAll();

int DeepDogFaceRecognizeResolveCanonicalId(int local_id);
bool DeepDogFaceRecognizeIsCanonical(int local_id);

bool DeepDogFaceRecognizeNeedsImmichName(int local_id);
bool DeepDogFaceRecognizeBindImmichName(int local_id, const char* display_name, const char* immich_person_id);
bool DeepDogFaceRecognizeBindImmichAsset(int local_id, const char* asset_id);

bool DeepDogFaceRecognizeRename(int local_id, const char* display_name);
bool DeepDogFaceRecognizeDeleteOne(int local_id);
bool DeepDogFaceRecognizeMergeAlias(int source_local_id, int target_local_id);

int DeepDogFaceRecognizeListCanonical(std::vector<DeepDogFaceEnrolledEntry>* out);
int DeepDogFaceRecognizeGetFeatCount();
int DeepDogFaceRecognizeGetMaxFeats();
size_t DeepDogFaceRecognizeFormatRegistryJson(char* buf, size_t buf_size);

typedef bool (*DeepDogFacePendingImmichVisitFn)(int local_id, const char* asset_id, void* ctx);
int DeepDogFaceRecognizeVisitPendingImmich(DeepDogFacePendingImmichVisitFn fn, void* ctx);

void DeepDogFaceRecognizeSetRegistryChangedCallback(std::function<void()> cb);

#if DEEP_DOG_FACE_AI_ENABLE && DEEP_DOG_FACE_RECOG_ENABLE && defined(CONFIG_IDF_TARGET_ESP32S3)
void DeepDogFaceRecognizeProcess(const dl::image::img_t& img, const std::list<dl::detect::result_t>& detect_raw,
                                 std::vector<DeepDogFaceBox>* boxes, DeepDogFaceSnapshot* snap_fields);
#endif
