#pragma once

#include <esp_err.h>
#include <stdint.h>

namespace dl {
class TensorBase;
}

/** internal 栈 worker：facedb enroll/delete/clear（Flash/FAT，禁止在 PSRAM 栈 dog_face_ai 内调用） */

bool DeepDogFaceFacedbInit();
void DeepDogFaceFacedbShutdown();
bool DeepDogFaceFacedbIsReady();

esp_err_t DeepDogFaceFacedbEnrollFeatSync(dl::TensorBase* feat);
esp_err_t DeepDogFaceFacedbDeleteFeatSync(uint16_t local_id);
esp_err_t DeepDogFaceFacedbClearAllSync();

/** clear_db：阻塞新 enroll/delete，并等待 worker 队列排空。 */
void DeepDogFaceFacedbQuiesceBegin();
void DeepDogFaceFacedbQuiesceEnd();
bool DeepDogFaceFacedbWaitIdle(int timeout_ms);

/** 仅 face_facedb worker 调用（face_recognize.cc） */
esp_err_t DeepDogFaceRecognizeFacedbEnrollFeat(dl::TensorBase* feat);
esp_err_t DeepDogFaceRecognizeFacedbDeleteFeat(uint16_t local_id);
esp_err_t DeepDogFaceRecognizeFacedbClearAllFeats();
