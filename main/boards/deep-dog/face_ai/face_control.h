#pragma once

#include "face_ai_types.h"

#include <cstddef>
#include <functional>
#include <vector>

/** 人脸统一控制层（S07）：MQTT / MCP / HTTP 共用。 */

void DeepDogFaceControlInit();

void DeepDogFaceControlSetDetectionEnabled(bool on);
bool DeepDogFaceControlIsDetectionEnabled();
void DeepDogFaceControlSetRecognitionEnabled(bool on);
bool DeepDogFaceControlIsRecognitionEnabled();

void DeepDogFaceControlSetPipeline(DeepDogFacePipeline pipeline);
DeepDogFacePipeline DeepDogFaceControlGetPipeline();
void DeepDogFaceControlSetDetectIntervalMs(int ms);
int DeepDogFaceControlGetDetectIntervalMs();

bool DeepDogFaceControlClearAll();
bool DeepDogFaceControlDeleteOne(int local_id);
bool DeepDogFaceControlRename(int local_id, const char* name);
bool DeepDogFaceControlMergeAlias(int source_local_id, int target_local_id);
bool DeepDogFaceControlRefreshImmich(int local_id);

int DeepDogFaceControlListEnrolled(std::vector<DeepDogFaceEnrolledEntry>* out);
size_t DeepDogFaceControlFormatRegistryJson(char* buf, size_t buf_size);
void DeepDogFaceControlCopySnapshot(DeepDogFaceSnapshot* out);

void DeepDogFaceControlSetRegistryChangedCallback(std::function<void()> cb);
