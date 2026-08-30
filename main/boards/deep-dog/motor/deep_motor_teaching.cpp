#include "config.h"
#if DEEP_DOG_MOTOR_ENABLE

#include "deep_motor_teaching.h"
#include "deep_motor.h"
#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <math.h>
#include <string.h>
#include <time.h>

static const char* TAG = "MotorTeaching";

namespace {

float clampTeachingFloat(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

float sampleTeachingPosition(uint16_t count, const float* positions, float u01) {
    if (count == 0 || positions == nullptr) {
        return 0.0f;
    }
    if (count == 1) {
        return positions[0];
    }
    u01 = clampTeachingFloat(u01, 0.0f, 1.0f);
    const float f = u01 * static_cast<float>(count - 1);
    const uint16_t i0 = static_cast<uint16_t>(floorf(f));
    uint16_t i1 = i0 + 1;
    if (i1 >= count) {
        return positions[count - 1];
    }
    const float frac = f - static_cast<float>(i0);
    return positions[i0] + (positions[i1] - positions[i0]) * frac;
}

float velocityFromPositionDelta(float pos_now, float pos_next, uint32_t dt_ms) {
    if (dt_ms == 0) {
        return 0.0f;
    }
    const float vel = (pos_next - pos_now) / (static_cast<float>(dt_ms) * 0.001f);
    return clampTeachingFloat(vel, V_MIN, V_MAX);
}

void sampleAtTimeline(const TeachingSample* samples, uint16_t count, uint32_t t_ms, float* pos_out, float* vel_out) {
    if (count == 0 || samples == nullptr) {
        if (pos_out) {
            *pos_out = 0.0f;
        }
        if (vel_out) {
            *vel_out = 0.0f;
        }
        return;
    }
    if (count == 1 || t_ms <= samples[0].t_ms) {
        if (pos_out) {
            *pos_out = samples[0].position_rad;
        }
        if (vel_out) {
            *vel_out = samples[0].velocity_rad_s;
        }
        return;
    }
    const uint32_t t_last = samples[count - 1].t_ms;
    if (t_ms >= t_last) {
        if (pos_out) {
            *pos_out = samples[count - 1].position_rad;
        }
        if (vel_out) {
            *vel_out = samples[count - 1].velocity_rad_s;
        }
        return;
    }
    uint16_t lo = 0;
    uint16_t hi = count - 1;
    while (hi - lo > 1) {
        const uint16_t mid = static_cast<uint16_t>((lo + hi) / 2);
        if (samples[mid].t_ms <= t_ms) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const TeachingSample& a = samples[lo];
    const TeachingSample& b = samples[hi];
    const uint32_t dt = b.t_ms - a.t_ms;
    float frac = 0.0f;
    if (dt > 0) {
        frac = static_cast<float>(t_ms - a.t_ms) / static_cast<float>(dt);
    }
    if (pos_out) {
        *pos_out = a.position_rad + (b.position_rad - a.position_rad) * frac;
    }
    if (vel_out) {
        *vel_out = a.velocity_rad_s + (b.velocity_rad_s - a.velocity_rad_s) * frac;
    }
}

#if DEEP_DOG_USE_MIT_WALK
bool prepareTeachingMitPlay(uint8_t motor_id) {
    if (!MotorProtocol::setMotorControlMode(motor_id)) {
        ESP_LOGE(TAG, "示教播放: 电机%u 设置运控模式失败", (unsigned)motor_id);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!MotorProtocol::enableMotor(motor_id)) {
        ESP_LOGE(TAG, "示教播放: 电机%u enable 失败", (unsigned)motor_id);
        return false;
    }
    return true;
}
#endif

TeachingPlayConfig normalizeTeachingPlayConfig(const TeachingPlayConfig* cfg) {
    TeachingPlayConfig out = cfg ? *cfg : TeachingPlayConfigDefault();
    if (out.duration_ms < 1000) {
        out.duration_ms = 1000;
    } else if (out.duration_ms > 120000) {
        out.duration_ms = 120000;
    }
    if (out.blend_ms > 5000) {
        out.blend_ms = 5000;
    }
    if (out.time_scale < 0.1f) {
        out.time_scale = 0.1f;
    } else if (out.time_scale > 10.0f) {
        out.time_scale = 10.0f;
    }
    out.kp = clampTeachingFloat(out.kp, KP_MIN, KP_MAX);
    out.kd = clampTeachingFloat(out.kd, KD_MIN, KD_MAX);
    out.tau_ff = clampTeachingFloat(out.tau_ff, T_FF_MIN, T_FF_MAX);
    return out;
}

uint32_t trackTimelineDurationMs(const TeachingTrack& track) {
    if (track.count < 2) {
        return 0;
    }
    return track.samples[track.count - 1].t_ms - track.samples[0].t_ms;
}

void appendSamplesToJson(cJSON* root, const TeachingTrack& track) {
    cJSON* arr = cJSON_CreateArray();
    for (uint16_t i = 0; i < track.count; ++i) {
        cJSON* s = cJSON_CreateObject();
        cJSON_AddNumberToObject(s, "t_ms", static_cast<double>(track.samples[i].t_ms));
        cJSON_AddNumberToObject(s, "pos", track.samples[i].position_rad);
        cJSON_AddNumberToObject(s, "vel", track.samples[i].velocity_rad_s);
        cJSON_AddItemToArray(arr, s);
    }
    cJSON_AddItemToObject(root, "samples", arr);
}

// 剔除录制起始的量程下限钳位毛刺点：reset/失能后首帧反馈 raw≈0 解码为 P_MIN(-12.57)。
// 仅当开头若干点贴下限、且随后出现非物理大跳变时剔除；电机真实停在下限附近不受影响。
void stripLeadingGlitchSamples(TeachingTrack& tr) {
    if (tr.count < 2) {
        return;
    }
    constexpr float kGlitchNearLimit = -12.0f;
    constexpr float kGlitchJumpRad = 3.0f;
    uint16_t first_valid = 0;
    while (first_valid < tr.count && tr.samples[first_valid].position_rad <= kGlitchNearLimit) {
        ++first_valid;
    }
    if (first_valid == 0 || first_valid >= tr.count) {
        return;
    }
    const float jump = fabsf(tr.samples[first_valid].position_rad - tr.samples[0].position_rad);
    if (jump < kGlitchJumpRad) {
        return;
    }
    const uint32_t t0 = tr.samples[first_valid].t_ms;
    for (uint16_t i = 0; i + first_valid < tr.count; ++i) {
        tr.samples[i] = tr.samples[i + first_valid];
        tr.samples[i].t_ms -= t0;
    }
    tr.count -= first_valid;
    ESP_LOGI(TAG, "剔除前导毛刺 %u 点", (unsigned)first_valid);
}

}  // namespace

MotorTeachingManager::MotorTeachingManager(DeepMotor* owner)
    : owner_(owner),
      recording_count_(0),
      play_config_(TeachingPlayConfigDefault()),
      recording_task_handle_(nullptr),
      execute_task_handle_(nullptr),
      play_motor_count_(0),
      active_teaching_motor_id_(-1) {
    memset(tracks_, 0, sizeof(tracks_));
    memset(play_motor_ids_, 0, sizeof(play_motor_ids_));
}

void MotorTeachingManager::shutdown() {
    if (recording_task_handle_ != nullptr) {
        vTaskDelete(recording_task_handle_);
        recording_task_handle_ = nullptr;
    }
    if (execute_task_handle_ != nullptr) {
        vTaskDelete(execute_task_handle_);
        execute_task_handle_ = nullptr;
    }
}

int MotorTeachingManager::trackSlotForMotor(uint8_t motor_id) const {
    for (int i = 0; i < MAX_MOTOR_COUNT; ++i) {
        if (tracks_[i].motor_id == motor_id && (tracks_[i].recording || tracks_[i].data_ready || tracks_[i].count > 0)) {
            return i;
        }
    }
    for (int i = 0; i < MAX_MOTOR_COUNT; ++i) {
        if (tracks_[i].motor_id == motor_id) {
            return i;
        }
    }
    return -1;
}

int MotorTeachingManager::allocTrackSlot(uint8_t motor_id) {
    const int existing = trackSlotForMotor(motor_id);
    if (existing >= 0) {
        return existing;
    }
    for (int i = 0; i < MAX_MOTOR_COUNT; ++i) {
        if (tracks_[i].motor_id == 0 && !tracks_[i].recording && tracks_[i].count == 0) {
            tracks_[i].motor_id = motor_id;
            return i;
        }
    }
    for (int i = 0; i < MAX_MOTOR_COUNT; ++i) {
        if (!tracks_[i].recording && tracks_[i].count == 0) {
            tracks_[i].motor_id = motor_id;
            return i;
        }
    }
    return -1;
}

void MotorTeachingManager::clearTrack(int slot) {
    if (slot < 0 || slot >= MAX_MOTOR_COUNT) {
        return;
    }
    const uint8_t mid = tracks_[slot].motor_id;
    memset(&tracks_[slot], 0, sizeof(TeachingTrack));
    tracks_[slot].motor_id = mid;
}

bool MotorTeachingManager::anyRecording() const {
    return recording_count_ > 0;
}

bool MotorTeachingManager::beginRecordSession(uint8_t motor_id, const TeachingRecordConfig& cfg, int slot) {
    if (slot < 0 || owner_ == nullptr) {
        return false;
    }
    if (!owner_->isMotorRegistered(motor_id)) {
        ESP_LOGE(TAG, "电机%u 未注册", (unsigned)motor_id);
        return false;
    }
    if (!MotorProtocol::resetMotor(motor_id)) {
        ESP_LOGE(TAG, "reset 电机%u 失败", (unsigned)motor_id);
        return false;
    }
    owner_->invalidateMotorCommandCache(motor_id);

    clearTrack(slot);
    tracks_[slot].motor_id = motor_id;
    tracks_[slot].recording = true;
    tracks_[slot].data_ready = false;
    tracks_[slot].count = 0;
    tracks_[slot].record_start_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
    tracks_[slot].sample_period_ms = cfg.sample_period_ms > 0 ? cfg.sample_period_ms : TEACHING_EPScan_RECORD_PERIOD_MS;
    tracks_[slot].epscan_n_saved = MotorProtocol::periodMsToEpScanN(TEACHING_EPScan_DEFAULT_PERIOD_MS);
    tracks_[slot].epscan_session = false;

    if (!MotorProtocol::setEpScanPeriodMs(motor_id, tracks_[slot].sample_period_ms)) {
        ESP_LOGW(TAG, "电机%u 设置 EPScan %ums 失败，仍尝试 T24 录制", (unsigned)motor_id,
                 (unsigned)tracks_[slot].sample_period_ms);
    } else {
        tracks_[slot].epscan_session = cfg.restore_epscan_on_stop;
        ESP_LOGI(TAG, "电机%u EPScan=%ums", (unsigned)motor_id, (unsigned)tracks_[slot].sample_period_ms);
    }

    if (!owner_->requestActiveReport(motor_id)) {
        ESP_LOGE(TAG, "电机%u 开启 T24 失败", (unsigned)motor_id);
        if (tracks_[slot].epscan_session) {
            (void)MotorProtocol::setEpScanPeriodMs(motor_id, TEACHING_EPScan_DEFAULT_PERIOD_MS);
        }
        tracks_[slot].recording = false;
        return false;
    }

    recording_count_++;
    active_teaching_motor_id_ = static_cast<int8_t>(motor_id);
    owner_->setActiveMotorId(motor_id);
    return true;
}

void MotorTeachingManager::endRecordSession(int slot, bool restore_epscan) {
    if (slot < 0 || slot >= MAX_MOTOR_COUNT) {
        return;
    }
    TeachingTrack& tr = tracks_[slot];
    if (tr.recording) {
        (void)owner_->releaseActiveReport(tr.motor_id);
        if (restore_epscan && tr.epscan_session) {
            (void)MotorProtocol::setEpScanPeriodMs(tr.motor_id, TEACHING_EPScan_DEFAULT_PERIOD_MS);
        }
        tr.recording = false;
        if (recording_count_ > 0) {
            recording_count_--;
        }
    }
    if (tr.count > 0) {
        stripLeadingGlitchSamples(tr);
        tr.data_ready = true;
    }

    if (tr.count > 0) {
        const TeachingSample& first = tr.samples[0];
        const TeachingSample& last = tr.samples[tr.count - 1];
        ESP_LOGI(TAG, "录制完成 motor=%u 点数=%u 首点(t=%ums,pos=%.3f) 末点(t=%ums,pos=%.3f)",
                 (unsigned)tr.motor_id, (unsigned)tr.count, (unsigned)first.t_ms, (double)first.position_rad,
                 (unsigned)last.t_ms, (double)last.position_rad);
        const uint16_t show = tr.count < 3 ? tr.count : 3;
        for (uint16_t i = 0; i < show; ++i) {
            ESP_LOGI(TAG, "  前3点[%u] t=%ums pos=%.3f vel=%.3f", (unsigned)i, (unsigned)tr.samples[i].t_ms,
                     (double)tr.samples[i].position_rad, (double)tr.samples[i].velocity_rad_s);
        }
    }
}

bool MotorTeachingManager::startTeaching(uint8_t motor_id, const TeachingRecordConfig* cfg) {
    if (anyRecording()) {
        ESP_LOGW(TAG, "已有录制会话，请先 stop");
        return false;
    }
    TeachingRecordConfig rc = cfg ? *cfg : TeachingRecordConfigDefault();
    const int slot = allocTrackSlot(motor_id);
    if (slot < 0) {
        ESP_LOGE(TAG, "无可用 track 槽");
        return false;
    }
    if (!beginRecordSession(motor_id, rc, slot)) {
        return false;
    }
    if (recording_task_handle_ == nullptr) {
        BaseType_t ret =
            xTaskCreate(recordingTask, "teaching_rec", 4096, this, 5, &recording_task_handle_);
        if (ret != pdPASS) {
            endRecordSession(slot, rc.restore_epscan_on_stop);
            ESP_LOGE(TAG, "创建录制任务失败");
            return false;
        }
    }
    ESP_LOGI(TAG, "开始录制 motor_id=%u period=%ums", (unsigned)motor_id, (unsigned)rc.sample_period_ms);
    return true;
}

bool MotorTeachingManager::startTeachingMulti(const uint8_t* motor_ids, uint8_t count,
                                              const TeachingRecordConfig* cfg) {
    if (motor_ids == nullptr || count == 0) {
        return false;
    }
    if (anyRecording()) {
        ESP_LOGW(TAG, "已有录制会话");
        return false;
    }
    TeachingRecordConfig rc = cfg ? *cfg : TeachingRecordConfigDefault();
    for (uint8_t i = 0; i < count; ++i) {
        const int slot = allocTrackSlot(motor_ids[i]);
        if (slot < 0) {
            stopTeaching();
            return false;
        }
        if (!beginRecordSession(motor_ids[i], rc, slot)) {
            stopTeaching();
            return false;
        }
    }
    if (recording_task_handle_ == nullptr) {
        BaseType_t ret =
            xTaskCreate(recordingTask, "teaching_rec", 4096, this, 5, &recording_task_handle_);
        if (ret != pdPASS) {
            stopTeaching();
            return false;
        }
    }
    ESP_LOGI(TAG, "多轴录制启动 count=%u", (unsigned)count);
    return true;
}

bool MotorTeachingManager::stopTeaching() {
    for (int i = 0; i < MAX_MOTOR_COUNT; ++i) {
        if (tracks_[i].recording) {
            ESP_LOGI(TAG, "停止录制 motor_id=%u 点数=%u", (unsigned)tracks_[i].motor_id,
                     (unsigned)tracks_[i].count);
            endRecordSession(i, true);
        }
    }
    if (recording_task_handle_ != nullptr) {
        vTaskDelete(recording_task_handle_);
        recording_task_handle_ = nullptr;
    }
    active_teaching_motor_id_ = -1;
    return true;
}

void MotorTeachingManager::onFeedback(uint8_t motor_id, float position_rad, float velocity_rad_s,
                                      uint32_t now_ms) {
    for (int i = 0; i < MAX_MOTOR_COUNT; ++i) {
        TeachingTrack& tr = tracks_[i];
        if (!tr.recording || tr.motor_id != motor_id || tr.count >= MAX_TEACHING_POINTS) {
            continue;
        }
        TeachingSample& s = tr.samples[tr.count];
        s.t_ms = now_ms - tr.record_start_ms;
        s.position_rad = position_rad;
        s.velocity_rad_s = velocity_rad_s;
        tr.count++;
        if (tr.count <= 3 || (tr.count % 20) == 0) {
            ESP_LOGI(TAG, "录制 motor=%u #%u t=%ums pos=%.3f vel=%.3f", (unsigned)motor_id,
                     (unsigned)tr.count, (unsigned)s.t_ms, (double)s.position_rad, (double)s.velocity_rad_s);
        }
        break;
    }
}

void MotorTeachingManager::recordingTask(void* param) {
    auto* mgr = static_cast<MotorTeachingManager*>(param);
    ESP_LOGI(TAG, "录制任务运行（T24 + MIT 查询帧兜底）");
    while (mgr->anyRecording()) {
        for (int i = 0; i < MAX_MOTOR_COUNT; ++i) {
            const TeachingTrack& tr = mgr->tracks_[i];
            if (tr.recording) {
                // 老电机无 T24：MIT 全 0 帧（失能 + kp=kd=tau=0）触发 0x02 反馈，不产生力矩
                (void)MotorProtocol::controlMotor(tr.motor_id, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(TEACHING_SAMPLE_RATE_MS));
    }
    ESP_LOGI(TAG, "录制任务结束");
    mgr->recording_task_handle_ = nullptr;
    vTaskDelete(nullptr);
}

void MotorTeachingManager::playTask(void* param) {
    auto* mgr = static_cast<MotorTeachingManager*>(param);
#if !DEEP_DOG_USE_MIT_WALK
    ESP_LOGW(TAG, "示教 MIT 播放需要 DEEP_DOG_USE_MIT_WALK=1");
    mgr->execute_task_handle_ = nullptr;
    vTaskDelete(nullptr);
    return;
#else
    const TeachingPlayConfig cfg = mgr->play_config_;
    DeepMotor* owner = mgr->owner_;

    for (uint8_t pi = 0; pi < mgr->play_motor_count_; ++pi) {
        const uint8_t mid = mgr->play_motor_ids_[pi];
        if (!prepareTeachingMitPlay(mid)) {
            mgr->execute_task_handle_ = nullptr;
            vTaskDelete(nullptr);
            return;
        }
    }

    for (uint8_t pi = 0; pi < mgr->play_motor_count_; ++pi) {
        const uint8_t mid = mgr->play_motor_ids_[pi];
        const TeachingTrack* tr = mgr->getTrack(mid);
        if (tr == nullptr || tr->count == 0) {
            continue;
        }
        float current_angle = tr->samples[0].position_rad;
        motor_status_t st {};
        if (owner->getMotorStatus(mid, &st)) {
            current_angle = st.current_angle;
        }
        const float p0 = tr->samples[0].position_rad;
        if (cfg.blend_ms > 0 && tr->count > 0) {
            uint32_t elapsed = 0;
            while (elapsed < cfg.blend_ms) {
                const float u = static_cast<float>(elapsed) / static_cast<float>(cfg.blend_ms);
                const float pos_now = current_angle + (p0 - current_angle) * u;
                const uint32_t next_elapsed =
                    (elapsed + TEACHING_PLAY_TICK_MS < cfg.blend_ms) ? elapsed + TEACHING_PLAY_TICK_MS : cfg.blend_ms;
                const float u_next = static_cast<float>(next_elapsed) / static_cast<float>(cfg.blend_ms);
                const float pos_next = current_angle + (p0 - current_angle) * u_next;
                const float vel = velocityFromPositionDelta(pos_now, pos_next, next_elapsed - elapsed);
                (void)owner->setMotorMitCommand(mid, pos_now, vel, cfg.kp, cfg.kd, cfg.tau_ff);
                vTaskDelay(pdMS_TO_TICKS(TEACHING_PLAY_TICK_MS));
                elapsed += TEACHING_PLAY_TICK_MS;
            }
        }
    }

    uint32_t timeline_ms = 0;
    bool use_timeline = cfg.use_recorded_timeline;
    for (uint8_t pi = 0; pi < mgr->play_motor_count_; ++pi) {
        const TeachingTrack* tr = mgr->getTrack(mgr->play_motor_ids_[pi]);
        if (tr == nullptr || tr->count < 2) {
            use_timeline = false;
            continue;
        }
        const uint32_t d = trackTimelineDurationMs(*tr);
        if (d > timeline_ms) {
            timeline_ms = d;
        }
    }
    if (timeline_ms == 0) {
        use_timeline = false;
    }

    if (use_timeline && timeline_ms > 0) {
        const uint32_t play_duration = static_cast<uint32_t>(static_cast<float>(timeline_ms) * cfg.time_scale);
        ESP_LOGI(TAG, "Phase B 时间轴 duration=%ums scale=%.2f", (unsigned)play_duration, (double)cfg.time_scale);
        uint32_t elapsed = 0;
        while (elapsed < play_duration) {
            const uint32_t track_t = static_cast<uint32_t>(static_cast<float>(elapsed) / cfg.time_scale);
            for (uint8_t pi = 0; pi < mgr->play_motor_count_; ++pi) {
                const uint8_t mid = mgr->play_motor_ids_[pi];
                const TeachingTrack* tr = mgr->getTrack(mid);
                if (tr == nullptr || tr->count == 0) {
                    continue;
                }
                float pos = 0.0f;
                float vel = 0.0f;
                sampleAtTimeline(tr->samples, tr->count, track_t + tr->samples[0].t_ms, &pos, &vel);
                vel /= cfg.time_scale;
                (void)owner->setMotorMitCommand(mid, pos, vel, cfg.kp, cfg.kd, cfg.tau_ff);
            }
            vTaskDelay(pdMS_TO_TICKS(TEACHING_PLAY_TICK_MS));
            elapsed += TEACHING_PLAY_TICK_MS;
        }
        for (uint8_t pi = 0; pi < mgr->play_motor_count_; ++pi) {
            const uint8_t mid = mgr->play_motor_ids_[pi];
            const TeachingTrack* tr = mgr->getTrack(mid);
            if (tr == nullptr || tr->count == 0) {
                continue;
            }
            const TeachingSample& last = tr->samples[tr->count - 1];
            (void)owner->setMotorMitCommand(mid, last.position_rad, 0.0f, cfg.kp, cfg.kd, cfg.tau_ff);
        }
    } else {
        const uint32_t dur = cfg.duration_ms;
        uint32_t elapsed = 0;
        while (elapsed < dur) {
            const float u = static_cast<float>(elapsed) / static_cast<float>(dur);
            const uint32_t next_elapsed =
                (elapsed + TEACHING_PLAY_TICK_MS < dur) ? elapsed + TEACHING_PLAY_TICK_MS : dur;
            const float u_next = static_cast<float>(next_elapsed) / static_cast<float>(dur);
            for (uint8_t pi = 0; pi < mgr->play_motor_count_; ++pi) {
                const uint8_t mid = mgr->play_motor_ids_[pi];
                const TeachingTrack* tr = mgr->getTrack(mid);
                if (tr == nullptr || tr->count == 0) {
                    continue;
                }
                float positions[MAX_TEACHING_POINTS];
                for (uint16_t i = 0; i < tr->count; ++i) {
                    positions[i] = tr->samples[i].position_rad;
                }
                const float pos_now = sampleTeachingPosition(tr->count, positions, u);
                const float pos_next = sampleTeachingPosition(tr->count, positions, u_next);
                const float vel = velocityFromPositionDelta(pos_now, pos_next, next_elapsed - elapsed);
                (void)owner->setMotorMitCommand(mid, pos_now, vel, cfg.kp, cfg.kd, cfg.tau_ff);
            }
            vTaskDelay(pdMS_TO_TICKS(TEACHING_PLAY_TICK_MS));
            elapsed += TEACHING_PLAY_TICK_MS;
        }
        for (uint8_t pi = 0; pi < mgr->play_motor_count_; ++pi) {
            const uint8_t mid = mgr->play_motor_ids_[pi];
            const TeachingTrack* tr = mgr->getTrack(mid);
            if (tr == nullptr || tr->count == 0) {
                continue;
            }
            float positions[MAX_TEACHING_POINTS];
            for (uint16_t i = 0; i < tr->count; ++i) {
                positions[i] = tr->samples[i].position_rad;
            }
            const float p_end = sampleTeachingPosition(tr->count, positions, 1.0f);
            (void)owner->setMotorMitCommand(mid, p_end, 0.0f, cfg.kp, cfg.kd, cfg.tau_ff);
        }
    }

    ESP_LOGI(TAG, "MIT 播放完成");
    mgr->execute_task_handle_ = nullptr;
    vTaskDelete(nullptr);
#endif
}

bool MotorTeachingManager::executeTeaching(uint8_t motor_id, const TeachingPlayConfig* cfg) {
#if !DEEP_DOG_USE_MIT_WALK
    ESP_LOGW(TAG, "示教 MIT 播放需要 DEEP_DOG_USE_MIT_WALK=1");
    return false;
#else
    return executeTeachingMulti(&motor_id, 1, cfg);
#endif
}

bool MotorTeachingManager::executeTeachingMulti(const uint8_t* motor_ids, uint8_t count,
                                                const TeachingPlayConfig* cfg) {
#if !DEEP_DOG_USE_MIT_WALK
    ESP_LOGW(TAG, "示教 MIT 播放需要 DEEP_DOG_USE_MIT_WALK=1");
    return false;
#else
    if (motor_ids == nullptr || count == 0 || owner_ == nullptr) {
        return false;
    }
    if (execute_task_handle_ != nullptr) {
        ESP_LOGW(TAG, "播放任务已在运行");
        return false;
    }
    for (uint8_t i = 0; i < count; ++i) {
        const TeachingTrack* tr = getTrack(motor_ids[i]);
        if (tr == nullptr || !tr->data_ready || tr->count == 0) {
            ESP_LOGE(TAG, "电机%u 无录制数据", (unsigned)motor_ids[i]);
            return false;
        }
        if (!owner_->isMotorRegistered(motor_ids[i])) {
            ESP_LOGE(TAG, "电机%u 未注册", (unsigned)motor_ids[i]);
            return false;
        }
    }
    play_config_ = normalizeTeachingPlayConfig(cfg);
    play_motor_count_ = count;
    memcpy(play_motor_ids_, motor_ids, count);
    owner_->setActiveMotorId(motor_ids[0]);

    for (uint8_t i = 0; i < count; ++i) {
        const TeachingTrack* tr = getTrack(motor_ids[i]);
        if (tr != nullptr && tr->count > 0) {
            const TeachingSample& first = tr->samples[0];
            const TeachingSample& last = tr->samples[tr->count - 1];
            ESP_LOGI(TAG, "播放轨迹 motor=%u 点数=%u 首点(t=%ums,pos=%.3f) 末点(t=%ums,pos=%.3f) blend=%ums",
                     (unsigned)motor_ids[i], (unsigned)tr->count, (unsigned)first.t_ms, (double)first.position_rad,
                     (unsigned)last.t_ms, (double)last.position_rad, (unsigned)play_config_.blend_ms);
        }
    }

    BaseType_t ret = xTaskCreate(playTask, "teaching_play", 4096, this, 5, &execute_task_handle_);
    if (ret != pdPASS) {
        execute_task_handle_ = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "播放启动 count=%u time_scale=%.2f", (unsigned)count, (double)play_config_.time_scale);
    return true;
#endif
}

bool MotorTeachingManager::isTeachingMode() const {
    return anyRecording();
}

bool MotorTeachingManager::isTeachingDataReady() const {
    for (int i = 0; i < MAX_MOTOR_COUNT; ++i) {
        if (tracks_[i].data_ready && tracks_[i].count > 0) {
            return true;
        }
    }
    return false;
}

uint16_t MotorTeachingManager::getTeachingPointCount() const {
    if (active_teaching_motor_id_ >= 0) {
        return getTeachingPointCount(static_cast<uint8_t>(active_teaching_motor_id_));
    }
    for (int i = 0; i < MAX_MOTOR_COUNT; ++i) {
        if (tracks_[i].data_ready) {
            return tracks_[i].count;
        }
    }
    return 0;
}

uint16_t MotorTeachingManager::getTeachingPointCount(uint8_t motor_id) const {
    const TeachingTrack* tr = getTrack(motor_id);
    return tr ? tr->count : 0;
}

const TeachingTrack* MotorTeachingManager::getTrack(uint8_t motor_id) const {
    const int slot = trackSlotForMotor(motor_id);
    if (slot < 0) {
        return nullptr;
    }
    return &tracks_[slot];
}

int8_t MotorTeachingManager::getActiveTeachingMotorId() const {
    return active_teaching_motor_id_;
}

char* MotorTeachingManager::buildSnapshotJson(uint8_t motor_id) const {
    const TeachingTrack* tr = getTrack(motor_id);
    if (tr == nullptr || tr->count == 0) {
        return nullptr;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "motor_id", motor_id);
    cJSON_AddNumberToObject(root, "point_count", tr->count);
    cJSON_AddNumberToObject(root, "duration_ms", static_cast<double>(trackTimelineDurationMs(*tr)));
    cJSON_AddNumberToObject(root, "sample_period_ms", static_cast<double>(tr->sample_period_ms));
    appendSamplesToJson(root, *tr);
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        cJSON_AddNumberToObject(root, "ts", static_cast<double>(now));
    }
    char* out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

char* MotorTeachingManager::buildMultiSnapshotJson(const uint8_t* motor_ids, uint8_t count) const {
    if (motor_ids == nullptr || count == 0) {
        return nullptr;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON* motors = cJSON_CreateArray();
    for (uint8_t i = 0; i < count; ++i) {
        const TeachingTrack* tr = getTrack(motor_ids[i]);
        if (tr == nullptr || tr->count == 0) {
            continue;
        }
        cJSON* m = cJSON_CreateObject();
        cJSON_AddNumberToObject(m, "motor_id", motor_ids[i]);
        cJSON_AddNumberToObject(m, "point_count", tr->count);
        cJSON_AddNumberToObject(m, "duration_ms", static_cast<double>(trackTimelineDurationMs(*tr)));
        cJSON_AddNumberToObject(m, "sample_period_ms", static_cast<double>(tr->sample_period_ms));
        appendSamplesToJson(m, *tr);
        cJSON_AddItemToArray(motors, m);
    }
    cJSON_AddItemToObject(root, "motors", motors);
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        cJSON_AddNumberToObject(root, "ts", static_cast<double>(now));
    }
    char* out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

char* MotorTeachingManager::buildTeachingStatusJson() const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "recording", anyRecording());
    cJSON_AddBoolToObject(root, "ready", isTeachingDataReady());
    if (active_teaching_motor_id_ >= 0) {
        cJSON_AddNumberToObject(root, "motor_id", active_teaching_motor_id_);
        const TeachingTrack* tr = getTrack(static_cast<uint8_t>(active_teaching_motor_id_));
        if (tr != nullptr) {
            cJSON_AddNumberToObject(root, "point_count", tr->count);
            cJSON_AddNumberToObject(root, "duration_ms", static_cast<double>(trackTimelineDurationMs(*tr)));
        }
    } else {
        for (int i = 0; i < MAX_MOTOR_COUNT; ++i) {
            if (tracks_[i].data_ready && tracks_[i].count > 0) {
                cJSON_AddNumberToObject(root, "motor_id", tracks_[i].motor_id);
                cJSON_AddNumberToObject(root, "point_count", tracks_[i].count);
                cJSON_AddNumberToObject(root, "duration_ms", static_cast<double>(trackTimelineDurationMs(tracks_[i])));
                break;
            }
        }
    }
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        cJSON_AddNumberToObject(root, "ts", static_cast<double>(now));
    }
    char* out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

#endif  // DEEP_DOG_MOTOR_ENABLE
