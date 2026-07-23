#pragma once

#include "sensor/imu_config.h"

#if DEEP_DOG_IMU_ENABLE

#include "bmi270_api.h"

/** 六轴：accel m/s²，gyro dps（对齐 thumble / YAML） */
struct DeepDogImuRawData {
    float accel_x = 0.0f;
    float accel_y = 0.0f;
    float accel_z = 0.0f;
    float gyro_x = 0.0f;
    float gyro_y = 0.0f;
    float gyro_z = 0.0f;
};

/**
 * BMI270（i2c_bus 组件句柄）。无芯片时 Initialize() 失败，调用方可仍发 ok=false。
 */
class DeepDogImuSensor {
public:
    explicit DeepDogImuSensor(i2c_bus_handle_t i2c_bus);
    ~DeepDogImuSensor();

    bool Initialize();
    bool ReadRawData(DeepDogImuRawData* out_data);
    bool IsInitialized() const { return initialized_; }

private:
    i2c_bus_handle_t i2c_bus_;
    bmi270_handle_t bmi_handle_ = nullptr;
    bool initialized_ = false;
};

#endif  // DEEP_DOG_IMU_ENABLE
