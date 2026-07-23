#include "sensor/imu_sensor.h"

#if DEEP_DOG_IMU_ENABLE

#include <cmath>
#include <cstring>
#include <esp_log.h>

#define TAG "dog_imu"

namespace {

constexpr float kGravityEarth = 9.80665f;

float LsbToMps2(int16_t val, float g_range, uint8_t bit_width) {
    const float half_scale = static_cast<float>(std::pow(2.0, static_cast<double>(bit_width)) / 2.0);
    return (kGravityEarth * static_cast<float>(val) * g_range) / half_scale;
}

float LsbToDps(int16_t val, float dps, uint8_t bit_width) {
    const float half_scale = static_cast<float>(std::pow(2.0, static_cast<double>(bit_width)) / 2.0);
    return (dps / half_scale) * static_cast<float>(val);
}

int8_t ConfigureAccelGyro(bmi270_handle_t dev) {
    struct bmi2_sens_config config[2];
    config[BMI2_ACCEL].type = BMI2_ACCEL;
    config[BMI2_GYRO].type = BMI2_GYRO;

    int8_t rslt = bmi2_get_sensor_config(config, 2, dev);
    if (rslt != BMI2_OK) {
        return rslt;
    }

    config[BMI2_ACCEL].cfg.acc.odr = BMI2_ACC_ODR_200HZ;
    config[BMI2_ACCEL].cfg.acc.range = BMI2_ACC_RANGE_2G;
    config[BMI2_ACCEL].cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
    config[BMI2_ACCEL].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;

    config[BMI2_GYRO].cfg.gyr.odr = BMI2_GYR_ODR_200HZ;
    config[BMI2_GYRO].cfg.gyr.range = BMI2_GYR_RANGE_2000;
    config[BMI2_GYRO].cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
    config[BMI2_GYRO].cfg.gyr.noise_perf = BMI2_POWER_OPT_MODE;
    config[BMI2_GYRO].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;

    return bmi2_set_sensor_config(config, 2, dev);
}

}  // namespace

DeepDogImuSensor::DeepDogImuSensor(i2c_bus_handle_t i2c_bus)
    : i2c_bus_(i2c_bus), bmi_handle_(nullptr), initialized_(false) {}

DeepDogImuSensor::~DeepDogImuSensor() {
    if (bmi_handle_ != nullptr) {
        bmi270_sensor_del(&bmi_handle_);
        bmi_handle_ = nullptr;
    }
}

bool DeepDogImuSensor::Initialize() {
    if (i2c_bus_ == nullptr) {
        ESP_LOGE(TAG, "I2C bus null, cannot init BMI270");
        initialized_ = false;
        return false;
    }

    esp_err_t ret = bmi270_sensor_create(i2c_bus_, &bmi_handle_, bmi270_config_file,
                                         BMI2_GYRO_CROSS_SENS_ENABLE | BMI2_CRT_RTOSK_ENABLE);
    if (ret != ESP_OK || !bmi_handle_) {
        ESP_LOGW(TAG, "BMI270 create failed: %s (board may have no chip)", esp_err_to_name(ret));
        bmi_handle_ = nullptr;
        initialized_ = false;
        return false;
    }

    int8_t rslt = ConfigureAccelGyro(bmi_handle_);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "ConfigureAccelGyro failed: %d", rslt);
        initialized_ = false;
        return false;
    }

    uint8_t sensor_list[2] = {BMI2_ACCEL, BMI2_GYRO};
    rslt = bmi2_sensor_enable(sensor_list, 2, bmi_handle_);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "bmi2_sensor_enable failed: %d", rslt);
        initialized_ = false;
        return false;
    }

    ESP_LOGI(TAG, "BMI270 initialized");
    initialized_ = true;
    return true;
}

bool DeepDogImuSensor::ReadRawData(DeepDogImuRawData* out_data) {
    if (!initialized_ || out_data == nullptr) {
        return false;
    }

    struct bmi2_sens_data sensor_data {};
    int8_t rslt = bmi2_get_sensor_data(&sensor_data, bmi_handle_);
    if (rslt != BMI2_OK) {
        ESP_LOGW(TAG, "bmi2_get_sensor_data failed: %d", rslt);
        return false;
    }

    const uint8_t resolution = bmi_handle_ ? bmi_handle_->resolution : 16;
    out_data->accel_x = LsbToMps2(sensor_data.acc.x, BMI2_ACC_RANGE_2G_VAL, resolution);
    out_data->accel_y = LsbToMps2(sensor_data.acc.y, BMI2_ACC_RANGE_2G_VAL, resolution);
    out_data->accel_z = LsbToMps2(sensor_data.acc.z, BMI2_ACC_RANGE_2G_VAL, resolution);
    out_data->gyro_x = LsbToDps(sensor_data.gyr.x, BMI2_GYR_RANGE_2000_VAL, resolution);
    out_data->gyro_y = LsbToDps(sensor_data.gyr.y, BMI2_GYR_RANGE_2000_VAL, resolution);
    out_data->gyro_z = LsbToDps(sensor_data.gyr.z, BMI2_GYR_RANGE_2000_VAL, resolution);
    return true;
}

#endif  // DEEP_DOG_IMU_ENABLE
