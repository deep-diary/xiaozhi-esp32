#pragma once

#include "face_ai_types.h"

#include <esp_timer.h>

#include <string>

class DeepDogMqttClient;

/** face/cmd + face/status + face/registry + person/active */
class DeepDogFaceMqtt {
public:
    explicit DeepDogFaceMqtt(DeepDogMqttClient* client);
    ~DeepDogFaceMqtt();

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    void InitRegistryHook();

    void OnConnected();
    void OnDisconnected();
    void Stop();

    void OnMessage(const std::string& topic, const std::string& payload);
    bool PublishStatus(bool force = false);
    bool PublishRegistry(bool force = false);
    bool PublishImmichStatus(bool force = false);

private:
    static void PollTimerCb(void* arg);
    static void RegistryPublishTimerCb(void* arg);

    void ScheduleRegistryPublish();
    void MaybePublishPersonActive(const struct DeepDogFaceSnapshot& snap);

    DeepDogMqttClient* client_;
    esp_timer_handle_t poll_timer_ = nullptr;
    esp_timer_handle_t registry_publish_timer_ = nullptr;
    bool enabled_ = false;
    std::string last_fingerprint_;
    std::string last_registry_fp_;
    std::string last_immich_fp_;
    int last_person_active_id_ = 0;
    uint32_t immich_ping_every_n_ = 0;
    uint32_t status_poll_n_ = 0;
};
