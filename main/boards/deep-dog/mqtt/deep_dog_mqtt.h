#pragma once

#include "mqtt/mqtt_config.h"

#include <memory>

class VisionFrameHub;
class DeepDogHttpServer;
class DeepDogImuSensor;

/**
 * deep-dog 板级 MQTT 门面：device + stream + imu。
 * StartNetwork 之后调用 Start()。
 */
class DeepDogMqtt {
public:
    DeepDogMqtt();
    ~DeepDogMqtt();

    void SetVisionHub(VisionFrameHub* hub);
    void SetHttpServer(DeepDogHttpServer* http);
    void SetHttpPort(int port);
    void SetImuSensor(DeepDogImuSensor* sensor);

    bool Start();
    void Stop();
    bool IsRunning() const;

private:
#if DEEP_DOG_MQTT_ENABLE
    struct Impl;
    std::unique_ptr<Impl> impl_;
#endif
};
