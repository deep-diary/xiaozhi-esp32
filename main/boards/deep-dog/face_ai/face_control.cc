#include "face_control.h"

#include "face_ai_bridge.h"
#include "face_recognize.h"
#include "immich_client.h"

void DeepDogFaceControlInit() {
    /* Registry MQTT 回调由 face_mqtt::InitRegistryHook 注册，勿在此覆盖 */
}

void DeepDogFaceControlSetDetectionEnabled(bool on) {
    DeepDogFaceAiSetEnabled(on);
}

bool DeepDogFaceControlIsDetectionEnabled() {
    return DeepDogFaceAiIsEnabled();
}

void DeepDogFaceControlSetRecognitionEnabled(bool on) {
    DeepDogFaceAiSetRecognitionEnabled(on);
}

bool DeepDogFaceControlIsRecognitionEnabled() {
    return DeepDogFaceAiIsRecognitionEnabled();
}

void DeepDogFaceControlSetPipeline(DeepDogFacePipeline pipeline) {
    DeepDogFaceAiSetPipeline(pipeline);
}

DeepDogFacePipeline DeepDogFaceControlGetPipeline() {
    return DeepDogFaceAiGetPipeline();
}

void DeepDogFaceControlSetDetectIntervalMs(int ms) {
    DeepDogFaceAiSetDetectIntervalMs(ms);
}

int DeepDogFaceControlGetDetectIntervalMs() {
    return DeepDogFaceAiGetDetectIntervalMs();
}

bool DeepDogFaceControlClearAll() {
    return DeepDogFaceAiClearDb();
}

bool DeepDogFaceControlDeleteOne(int local_id) {
#if DEEP_DOG_FACE_RECOG_ENABLE
    return DeepDogFaceRecognizeDeleteOne(local_id);
#else
    (void)local_id;
    return false;
#endif
}

bool DeepDogFaceControlRename(int local_id, const char* name) {
#if DEEP_DOG_FACE_RECOG_ENABLE
    return DeepDogFaceRecognizeRename(local_id, name);
#else
    (void)local_id;
    (void)name;
    return false;
#endif
}

bool DeepDogFaceControlMergeAlias(int source_local_id, int target_local_id) {
#if DEEP_DOG_FACE_RECOG_ENABLE
    return DeepDogFaceRecognizeMergeAlias(source_local_id, target_local_id);
#else
    (void)source_local_id;
    (void)target_local_id;
    return false;
#endif
}

bool DeepDogFaceControlRefreshImmich(int local_id) {
#if DEEP_DOG_FACE_IMMICH_ENABLE
    DeepDogImmichRequestRefresh(local_id);
    return DeepDogImmichPollStoredAsset(local_id);
#else
    (void)local_id;
    return false;
#endif
}

int DeepDogFaceControlListEnrolled(std::vector<DeepDogFaceEnrolledEntry>* out) {
#if DEEP_DOG_FACE_RECOG_ENABLE
    return DeepDogFaceRecognizeListCanonical(out);
#else
    if (out) {
        out->clear();
    }
    return 0;
#endif
}

size_t DeepDogFaceControlFormatRegistryJson(char* buf, size_t buf_size) {
#if DEEP_DOG_FACE_RECOG_ENABLE
    return DeepDogFaceRecognizeFormatRegistryJson(buf, buf_size);
#else
    if (!buf || buf_size < 16) {
        return 0;
    }
    return static_cast<size_t>(snprintf(buf, buf_size, "{\"version\":1,\"count\":0,\"entries\":[],\"ts\":0}"));
#endif
}

void DeepDogFaceControlCopySnapshot(DeepDogFaceSnapshot* out) {
    DeepDogFaceAiCopySnapshot(out);
}

void DeepDogFaceControlSetRegistryChangedCallback(std::function<void()> cb) {
#if DEEP_DOG_FACE_RECOG_ENABLE
    DeepDogFaceRecognizeSetRegistryChangedCallback(std::move(cb));
#else
    (void)cb;
#endif
}
