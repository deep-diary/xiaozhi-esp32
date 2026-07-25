#pragma once

#include "sensor/imu_config.h"

#if DEEP_DOG_IMU_ENABLE

#include "sensor/imu_sensor.h"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include <cstdint>
#include <functional>

/** 12 路边沿开关 id（与 YAML switches.* 一致） */
enum class DeepDogImuSwitchId : uint8_t {
    kRotXPos = 0,
    kRotXNeg,
    kRotYPos,
    kRotYNeg,
    kRotZPos,
    kRotZNeg,
    kTransXPos,
    kTransXNeg,
    kTransYPos,
    kTransYNeg,
    kTransZPos,
    kTransZNeg,
    kCount,
};

constexpr int kDeepDogImuSwitchCount = static_cast<int>(DeepDogImuSwitchId::kCount);

const char* DeepDogImuSwitchIdName(DeepDogImuSwitchId id);

/**
 * 100 Hz 本地采集 + 12 路边沿识别 + 调度入口。
 * MQTT 经 GetLatest / TakeSwitchCounts 取快照与本周期计数（不再直接读芯片）。
 */
class DeepDogImuSwitch {
public:
    using Handler = std::function<void(DeepDogImuSwitchId id, float magnitude)>;

    explicit DeepDogImuSwitch(DeepDogImuSensor* sensor);
    ~DeepDogImuSwitch();

    void SetHandler(Handler handler) { handler_ = std::move(handler); }

    bool Start();
    void Stop();

    /** 拷贝最新六轴；ok=false 表示尚未成功读到或传感器未就绪 */
    void GetLatest(DeepDogImuRawData* out_raw, bool* out_ok) const;

    /** 拷贝自上次 Take 以来的边沿次数到 out[12]，然后清零 */
    void TakeSwitchCounts(int out_counts[kDeepDogImuSwitchCount]);

private:
    static void TimerCb(void* arg);
    void OnSample();
    void Fire(DeepDogImuSwitchId id, float magnitude);

    DeepDogImuSensor* sensor_ = nullptr;
    esp_timer_handle_t timer_ = nullptr;
    Handler handler_;

    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    DeepDogImuRawData latest_{};
    bool latest_ok_ = false;
    int counts_[kDeepDogImuSwitchCount] = {};

    float angle_deg_[3] = {};
    float gravity_[3] = {};
    float last_lin_[3] = {};
    bool gravity_inited_ = false;
    int64_t rot_cooldown_until_us_[3] = {};
    int64_t trans_cooldown_until_us_[3] = {};
    int64_t trans_suppress_until_us_ = 0;
    int64_t last_sample_us_ = 0;
    bool started_ = false;
};

#endif  // DEEP_DOG_IMU_ENABLE
