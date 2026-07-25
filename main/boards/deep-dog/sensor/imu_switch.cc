#include "sensor/imu_switch.h"

#if DEEP_DOG_IMU_ENABLE

#include <esp_log.h>

#include <cmath>
#include <cstring>

#define TAG "dog_imu_sw"

namespace {

/** 一阶重力 LPF：约 0.7 Hz @ 100 Hz（alpha ≈ 1 - exp(-2π f dt)） */
constexpr float kGravityLpfAlpha = 0.04f;

const char* kSwitchNames[kDeepDogImuSwitchCount] = {
    "rot_x_pos",   "rot_x_neg",   "rot_y_pos",   "rot_y_neg",   "rot_z_pos",   "rot_z_neg",
    "trans_x_pos", "trans_x_neg", "trans_y_pos", "trans_y_neg", "trans_z_pos", "trans_z_neg",
};

DeepDogImuSwitchId RotId(int axis, bool positive) {
    const int base = axis * 2;
    return static_cast<DeepDogImuSwitchId>(base + (positive ? 0 : 1));
}

DeepDogImuSwitchId TransId(int axis, bool positive) {
    const int base = 6 + axis * 2;
    return static_cast<DeepDogImuSwitchId>(base + (positive ? 0 : 1));
}

}  // namespace

const char* DeepDogImuSwitchIdName(DeepDogImuSwitchId id) {
    const int i = static_cast<int>(id);
    if (i < 0 || i >= kDeepDogImuSwitchCount) {
        return "unknown";
    }
    return kSwitchNames[i];
}

DeepDogImuSwitch::DeepDogImuSwitch(DeepDogImuSensor* sensor) : sensor_(sensor) {
    esp_timer_create_args_t args = {
        .callback = &DeepDogImuSwitch::TimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_imu_sw",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &timer_);
}

DeepDogImuSwitch::~DeepDogImuSwitch() {
    Stop();
    if (timer_) {
        esp_timer_delete(timer_);
        timer_ = nullptr;
    }
}

bool DeepDogImuSwitch::Start() {
    if (started_ || !timer_) {
        return started_;
    }
    last_sample_us_ = 0;
    gravity_inited_ = false;
    trans_suppress_until_us_ = 0;
    std::memset(angle_deg_, 0, sizeof(angle_deg_));
    std::memset(rot_cooldown_until_us_, 0, sizeof(rot_cooldown_until_us_));
    std::memset(trans_cooldown_until_us_, 0, sizeof(trans_cooldown_until_us_));
    esp_timer_stop(timer_);
    esp_err_t err = esp_timer_start_periodic(timer_, DEEP_DOG_IMU_SAMPLE_INTERVAL_US);
    started_ = (err == ESP_OK);
    if (started_) {
        ESP_LOGI(TAG, "started (rot=%.0fdeg/%dms trans=%.1fm/s2/%dms suppress_after_rot=%dms)",
                 static_cast<double>(DEEP_DOG_IMU_ROT_THRESHOLD_DEG), DEEP_DOG_IMU_ROT_COOLDOWN_MS,
                 static_cast<double>(DEEP_DOG_IMU_TRANS_THRESHOLD_MPS2), DEEP_DOG_IMU_TRANS_COOLDOWN_MS,
                 DEEP_DOG_IMU_TRANS_SUPPRESS_AFTER_ROT_MS);
    } else {
        ESP_LOGE(TAG, "esp_timer_start_periodic failed: %s", esp_err_to_name(err));
    }
    return started_;
}

void DeepDogImuSwitch::Stop() {
    if (timer_) {
        esp_timer_stop(timer_);
    }
    started_ = false;
}

void DeepDogImuSwitch::TimerCb(void* arg) {
    auto* self = static_cast<DeepDogImuSwitch*>(arg);
    if (self) {
        self->OnSample();
    }
}

void DeepDogImuSwitch::GetLatest(DeepDogImuRawData* out_raw, bool* out_ok) const {
    if (!out_raw && !out_ok) {
        return;
    }
    portENTER_CRITICAL(&lock_);
    if (out_raw) {
        *out_raw = latest_;
    }
    if (out_ok) {
        *out_ok = latest_ok_;
    }
    portEXIT_CRITICAL(&lock_);
}

void DeepDogImuSwitch::TakeSwitchCounts(int out_counts[kDeepDogImuSwitchCount]) {
    if (!out_counts) {
        return;
    }
    portENTER_CRITICAL(&lock_);
    for (int i = 0; i < kDeepDogImuSwitchCount; ++i) {
        out_counts[i] = counts_[i];
        counts_[i] = 0;
    }
    portEXIT_CRITICAL(&lock_);
}

void DeepDogImuSwitch::Fire(DeepDogImuSwitchId id, float magnitude) {
    const int idx = static_cast<int>(id);
    if (idx < 0 || idx >= kDeepDogImuSwitchCount) {
        return;
    }
    portENTER_CRITICAL(&lock_);
    ++counts_[idx];
    portEXIT_CRITICAL(&lock_);

    if (handler_) {
        handler_(id, magnitude);
    } else {
        ESP_LOGI(TAG, "%s mag=%.1f", DeepDogImuSwitchIdName(id), static_cast<double>(magnitude));
    }
}

void DeepDogImuSwitch::OnSample() {
    const int64_t now_us = esp_timer_get_time();
    float dt_s = DEEP_DOG_IMU_SAMPLE_INTERVAL_US / 1e6f;
    if (last_sample_us_ > 0) {
        const float measured = static_cast<float>(now_us - last_sample_us_) / 1e6f;
        if (measured > 0.001f && measured < 0.1f) {
            dt_s = measured;
        }
    }
    last_sample_us_ = now_us;

    bool ok = false;
    DeepDogImuRawData raw{};
    if (sensor_ && sensor_->IsInitialized() && sensor_->ReadRawData(&raw)) {
        ok = true;
    }

    portENTER_CRITICAL(&lock_);
    latest_ = raw;
    latest_ok_ = ok;
    portEXIT_CRITICAL(&lock_);

    if (!ok) {
        return;
    }

    // ---- rotation: integrate gyro (dps → deg), ignore bias with deadzone ----
    constexpr float kGyroDeadzoneDps = 8.0f;  // 静置 bias ~0.3–1 dps；手势通常 >> 此值
    const float gyro_raw[3] = {raw.gyro_x, raw.gyro_y, raw.gyro_z};
    float gyro[3];
    for (int a = 0; a < 3; ++a) {
        gyro[a] = (std::fabs(gyro_raw[a]) < kGyroDeadzoneDps) ? 0.0f : gyro_raw[a];
    }
    bool any_gyro = false;
    for (int a = 0; a < 3; ++a) {
        if (gyro[a] != 0.0f) {
            any_gyro = true;
        }
        if (now_us >= rot_cooldown_until_us_[a]) {
            angle_deg_[a] += gyro[a] * dt_s;
        }
    }
    // 无显著旋转时缓慢回零，避免死区外残余漂移堆到阈值
    if (!any_gyro) {
        for (int a = 0; a < 3; ++a) {
            angle_deg_[a] *= 0.98f;
            if (std::fabs(angle_deg_[a]) < 0.5f) {
                angle_deg_[a] = 0.0f;
            }
        }
    }

    bool fired_rot = false;
    int best_rot_axis = -1;
    float best_rot_abs = 0.0f;
    for (int a = 0; a < 3; ++a) {
        if (now_us < rot_cooldown_until_us_[a]) {
            continue;
        }
        const float abs_a = std::fabs(angle_deg_[a]);
        if (abs_a >= DEEP_DOG_IMU_ROT_THRESHOLD_DEG && abs_a > best_rot_abs) {
            best_rot_abs = abs_a;
            best_rot_axis = a;
        }
    }
    if (best_rot_axis >= 0) {
        const bool pos = angle_deg_[best_rot_axis] >= 0.0f;
        Fire(RotId(best_rot_axis, pos), best_rot_abs);
        angle_deg_[best_rot_axis] = 0.0f;
        // 其它轴也清零，减少耦合连发
        for (int a = 0; a < 3; ++a) {
            angle_deg_[a] = 0.0f;
            rot_cooldown_until_us_[a] =
                now_us + static_cast<int64_t>(DEEP_DOG_IMU_ROT_COOLDOWN_MS) * 1000LL;
        }
        trans_suppress_until_us_ =
            now_us + static_cast<int64_t>(DEEP_DOG_IMU_TRANS_SUPPRESS_AFTER_ROT_MS) * 1000LL;
        fired_rot = true;
    }

    // ---- translation: gravity LPF + linear impulse ----
    if (fired_rot || now_us < trans_suppress_until_us_) {
        // 仍更新重力与 last_lin，便于抑制结束后状态连续
        const float accel_hold[3] = {raw.accel_x, raw.accel_y, raw.accel_z};
        if (!gravity_inited_) {
            gravity_[0] = accel_hold[0];
            gravity_[1] = accel_hold[1];
            gravity_[2] = accel_hold[2];
            gravity_inited_ = true;
        } else {
            for (int a = 0; a < 3; ++a) {
                gravity_[a] = gravity_[a] + kGravityLpfAlpha * (accel_hold[a] - gravity_[a]);
                last_lin_[a] = accel_hold[a] - gravity_[a];
            }
        }
        return;
    }

    const float accel[3] = {raw.accel_x, raw.accel_y, raw.accel_z};
    if (!gravity_inited_) {
        gravity_[0] = accel[0];
        gravity_[1] = accel[1];
        gravity_[2] = accel[2];
        gravity_inited_ = true;
        return;
    }
    for (int a = 0; a < 3; ++a) {
        gravity_[a] = gravity_[a] + kGravityLpfAlpha * (accel[a] - gravity_[a]);
    }

    float lin[3];
    int best_trans_axis = -1;
    float best_trans_abs = 0.0f;
    for (int a = 0; a < 3; ++a) {
        lin[a] = accel[a] - gravity_[a];
        last_lin_[a] = lin[a];
        if (now_us < trans_cooldown_until_us_[a]) {
            continue;
        }
        const float abs_l = std::fabs(lin[a]);
        if (abs_l >= DEEP_DOG_IMU_TRANS_THRESHOLD_MPS2 && abs_l > best_trans_abs) {
            best_trans_abs = abs_l;
            best_trans_axis = a;
        }
    }
    if (best_trans_axis >= 0) {
        const bool pos = lin[best_trans_axis] >= 0.0f;
        Fire(TransId(best_trans_axis, pos), best_trans_abs);
        for (int a = 0; a < 3; ++a) {
            trans_cooldown_until_us_[a] =
                now_us + static_cast<int64_t>(DEEP_DOG_IMU_TRANS_COOLDOWN_MS) * 1000LL;
        }
    }
}

#endif  // DEEP_DOG_IMU_ENABLE
