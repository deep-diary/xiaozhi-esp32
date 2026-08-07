#ifndef DEEP_MOTOR_TEACHING_H__
#define DEEP_MOTOR_TEACHING_H__

#include "protocol_motor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stdbool.h>

/** 与 deep_motor.h 对齐，避免循环包含 */
#ifndef MAX_MOTOR_COUNT
#define MAX_MOTOR_COUNT 13
#endif

#define MAX_TEACHING_POINTS 300
#define TEACHING_SAMPLE_RATE_MS 50
#define TEACHING_EPScan_DEFAULT_PERIOD_MS 10
#define TEACHING_EPScan_RECORD_PERIOD_MS 50
#define TEACHING_PLAY_DURATION_MS_DEFAULT 10000
#define TEACHING_PLAY_BLEND_MS_DEFAULT 3000
#define TEACHING_PLAY_TICK_MS 20
#define TEACHING_PLAY_KP_DEFAULT 1.0f
#define TEACHING_PLAY_KD_DEFAULT 1.0f
#define TEACHING_PLAY_TAU_FF_DEFAULT 0.0f
#define TEACHING_TIME_SCALE_DEFAULT 1.0f

struct TeachingSample {
    uint32_t t_ms;
    float position_rad;
    float velocity_rad_s;
};

struct TeachingRecordConfig {
    uint32_t sample_period_ms;
    bool restore_epscan_on_stop;
};

struct TeachingPlayConfig {
    uint32_t duration_ms;
    uint32_t blend_ms;
    float kp;
    float kd;
    float tau_ff;
    float time_scale;
    bool use_recorded_timeline;
};

static inline TeachingRecordConfig TeachingRecordConfigDefault() {
    return TeachingRecordConfig{TEACHING_EPScan_RECORD_PERIOD_MS, true};
}

static inline TeachingPlayConfig TeachingPlayConfigDefault() {
    return TeachingPlayConfig{TEACHING_PLAY_DURATION_MS_DEFAULT,
                              TEACHING_PLAY_BLEND_MS_DEFAULT,
                              TEACHING_PLAY_KP_DEFAULT,
                              TEACHING_PLAY_KD_DEFAULT,
                              TEACHING_PLAY_TAU_FF_DEFAULT,
                              TEACHING_TIME_SCALE_DEFAULT,
                              true};
}

struct TeachingTrack {
    uint8_t motor_id;
    bool recording;
    bool data_ready;
    TeachingSample samples[MAX_TEACHING_POINTS];
    uint16_t count;
    uint32_t record_start_ms;
    uint8_t epscan_n_saved;
    bool epscan_session;
    uint32_t sample_period_ms;
};

class DeepMotor;

/** 示教录制/播放（MOT-12）；由 DeepMotor 持有并委托 */
class MotorTeachingManager {
public:
    explicit MotorTeachingManager(DeepMotor* owner);

    bool startTeaching(uint8_t motor_id, const TeachingRecordConfig* cfg = nullptr);
    bool startTeachingMulti(const uint8_t* motor_ids, uint8_t count, const TeachingRecordConfig* cfg = nullptr);
    bool stopTeaching();
    bool executeTeaching(uint8_t motor_id, const TeachingPlayConfig* cfg = nullptr);
    bool executeTeachingMulti(const uint8_t* motor_ids, uint8_t count, const TeachingPlayConfig* cfg = nullptr);

    void onFeedback(uint8_t motor_id, float position_rad, float velocity_rad_s, uint32_t now_ms);

    bool isTeachingMode() const;
    bool isTeachingDataReady() const;
    uint16_t getTeachingPointCount() const;
    uint16_t getTeachingPointCount(uint8_t motor_id) const;
    const TeachingTrack* getTrack(uint8_t motor_id) const;
    int8_t getActiveTeachingMotorId() const;

    /** 调用方 cJSON_free */
    char* buildSnapshotJson(uint8_t motor_id) const;
    char* buildMultiSnapshotJson(const uint8_t* motor_ids, uint8_t count) const;
    char* buildTeachingStatusJson() const;

    void shutdown();

private:
    DeepMotor* owner_;
    TeachingTrack tracks_[MAX_MOTOR_COUNT];
    uint8_t recording_count_;
    TeachingPlayConfig play_config_;
    TaskHandle_t recording_task_handle_;
    TaskHandle_t execute_task_handle_;
    uint8_t play_motor_ids_[MAX_MOTOR_COUNT];
    uint8_t play_motor_count_;
    int8_t active_teaching_motor_id_;

    int trackSlotForMotor(uint8_t motor_id) const;
    int allocTrackSlot(uint8_t motor_id);
    void clearTrack(int slot);
    bool beginRecordSession(uint8_t motor_id, const TeachingRecordConfig& cfg, int slot);
    void endRecordSession(int slot, bool restore_epscan);
    bool anyRecording() const;

    static void recordingTask(void* param);
    static void playTask(void* param);
};

#endif  // DEEP_MOTOR_TEACHING_H__
