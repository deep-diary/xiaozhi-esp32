#include "mqtt/modules/imu_datapoints.h"

#include "config.h"
#include "sensor/imu_config.h"
#if DEEP_DOG_IMU_ENABLE
#include "sensor/imu_sensor.h"
#include "sensor/imu_switch.h"
#endif

#include <esp_log.h>
#include <esp_timer.h>

#include <cmath>
#include <climits>

#define TAG "imu_dp"

namespace {

#if DEEP_DOG_IMU_ENABLE

DeepDogImuSensor* s_sensor = nullptr;
DeepDogImuSwitch* s_hub = nullptr;

struct ImuSample {
    bool ok = false;
    float ax = 0, ay = 0, az = 0;
    float gx = 0, gy = 0, gz = 0;
    float accel_g = 0;
    float pitch = 0, roll = 0;
};

int64_t s_cache_us = 0;
ImuSample s_cache;

void AccelToPitchRoll(float ax, float ay, float az, float* pitch_deg, float* roll_deg) {
    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    if (pitch_deg) {
        *pitch_deg = std::atan2(-ax, std::sqrt(ay * ay + az * az)) * kRadToDeg;
    }
    if (roll_deg) {
        *roll_deg = std::atan2(ay, az) * kRadToDeg;
    }
}

// 50ms 内复用同一快照，避免一个发布周期内多次 I2C 读取
const ImuSample& GetSample() {
    const int64_t now = esp_timer_get_time();
    if (s_cache_us != 0 && (now - s_cache_us) < 50000) {
        return s_cache;
    }

    ImuSample s{};
    DeepDogImuRawData raw{};
    bool ok = false;
    if (s_hub) {
        s_hub->GetLatest(&raw, &ok);
    } else if (s_sensor && s_sensor->IsInitialized()) {
        ok = s_sensor->ReadRawData(&raw);
    }
    if (ok) {
        s.ok = true;
        s.ax = raw.accel_x;
        s.ay = raw.accel_y;
        s.az = raw.accel_z;
        s.gx = raw.gyro_x;
        s.gy = raw.gyro_y;
        s.gz = raw.gyro_z;
        s.accel_g = std::sqrt(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
        AccelToPitchRoll(s.ax, s.ay, s.az, &s.pitch, &s.roll);
    }
    s_cache = s;
    s_cache_us = now;
    return s_cache;
}

bool ReadOk(void* ctx) { (void)ctx; return GetSample().ok; }
float ReadAx(void* ctx) { (void)ctx; return GetSample().ax; }
float ReadAy(void* ctx) { (void)ctx; return GetSample().ay; }
float ReadAz(void* ctx) { (void)ctx; return GetSample().az; }
float ReadAccelG(void* ctx) { (void)ctx; return GetSample().accel_g; }
float ReadGx(void* ctx) { (void)ctx; return GetSample().gx; }
float ReadGy(void* ctx) { (void)ctx; return GetSample().gy; }
float ReadGz(void* ctx) { (void)ctx; return GetSample().gz; }
float ReadPitch(void* ctx) { (void)ctx; return GetSample().pitch; }
float ReadRoll(void* ctx) { (void)ctx; return GetSample().roll; }

#endif  // DEEP_DOG_IMU_ENABLE

}  // namespace

#define DP_IMU_FLOAT_POINT(id, name, desc, reader)                     \
    {                                                                  \
        id, name, "imu", nullptr, desc,                                \
        DP_TYPE_FLOAT, "", 3,                                          \
        -100000.0f, 100000.0f, 0.0f,                                   \
        nullptr,                                                       \
        DP_ACCESS_RO,                                                  \
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP | DP_CH_TELEMETRY,         \
        DP_POLICY_PERIODIC, 100, 0,                                    \
        nullptr, nullptr,                                              \
        { .read_float = reader }, { nullptr },                         \
    }

#if DEEP_DOG_IMU_ENABLE

static const DataPointDef kImuDataPoints[kImuDataPointCount] = {
    {
        "imu.ok", "可用", "imu", nullptr, "IMU 数据是否有效",
        DP_TYPE_BOOL, "", 0, 0, 0, 0,
        nullptr,
        DP_ACCESS_RO, DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_ON_CHANGE, 0, 0,
        nullptr, nullptr,
        { .read_bool = ReadOk }, { nullptr },
    },
    DP_IMU_FLOAT_POINT("imu.accel_x", "加速度X", "X 轴加速度", ReadAx),
    DP_IMU_FLOAT_POINT("imu.accel_y", "加速度Y", "Y 轴加速度", ReadAy),
    DP_IMU_FLOAT_POINT("imu.accel_z", "加速度Z", "Z 轴加速度", ReadAz),
    DP_IMU_FLOAT_POINT("imu.accel_g", "合加速度", "加速度模值", ReadAccelG),
    DP_IMU_FLOAT_POINT("imu.gyro_x", "角速度X", "X 轴角速度", ReadGx),
    DP_IMU_FLOAT_POINT("imu.gyro_y", "角速度Y", "Y 轴角速度", ReadGy),
    DP_IMU_FLOAT_POINT("imu.gyro_z", "角速度Z", "Z 轴角速度", ReadGz),
    DP_IMU_FLOAT_POINT("imu.pitch", "俯仰角", "俯仰角（度）", ReadPitch),
    DP_IMU_FLOAT_POINT("imu.roll", "横滚角", "横滚角（度）", ReadRoll),
};

static const DataModuleDef kImuModuleDef = {
    "imu",
    "IMU 传感器",
    "姿态、加速度与角速度（高频）",
    kImuDataPoints,
    kImuDataPointCount,
    false,
};

#else

static const DataPointDef kImuDataPoints[1] = {};
static const DataModuleDef kImuModuleDef = {"imu", "IMU 传感器", "IMU（未启用）", kImuDataPoints, 0, false};

#endif

void ImuDataPoints_Register(void* sensor, void* hub) {
#if DEEP_DOG_IMU_ENABLE
    s_sensor = static_cast<DeepDogImuSensor*>(sensor);
    s_hub = static_cast<DeepDogImuSwitch*>(hub);
    auto& reg = DataPointRegistry::Instance();
    bool ok = reg.RegisterModule(kImuModuleDef);
    ESP_LOGI(TAG, "register imu data points: ok=%d count=%u", ok ? 1 : 0,
             (unsigned)kImuDataPointCount);
#else
    (void)sensor;
    (void)hub;
    ESP_LOGI(TAG, "imu disabled, skip registration");
#endif
}

const DataModuleDef* ImuDataPoints_GetModuleDef() {
    return &kImuModuleDef;
}
