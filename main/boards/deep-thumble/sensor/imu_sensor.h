#ifndef IMU_SENSOR_H
#define IMU_SENSOR_H

#include "bmi270_api.h"  // 引入 BMI270 及 i2c_bus 类型定义

// 简单的六轴原始数据结构（加速度 + 陀螺仪）
struct ImuRawData {
    float accel_x = 0.0f;
    float accel_y = 0.0f;
    float accel_z = 0.0f;

    float gyro_x = 0.0f;
    float gyro_y = 0.0f;
    float gyro_z = 0.0f;
};

class ImuSensor {
public:
    explicit ImuSensor(i2c_bus_handle_t i2c_bus);
    ~ImuSensor();

    // 初始化 BMI270
    bool Initialize();

    // 读取一次原始数据（加速度 + 陀螺仪，单位：m/s^2 与 dps）
    bool ReadRawData(ImuRawData* out_data);

    bool IsInitialized() const { return initialized_; }

private:
    i2c_bus_handle_t i2c_bus_;
    bmi270_handle_t bmi_handle_ = nullptr;
    bool initialized_;
};

#endif // IMU_SENSOR_H

