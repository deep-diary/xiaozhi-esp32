#pragma once

#include <functional>
#include <string>
#include <vector>
#include <cstdint>

#include "frame_queue.hpp"

struct QueuedFrame;

struct FaceRecord {
    int id = -1;
    std::string name;
    int64_t last_seen_ms = 0;
};

struct FaceBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int id = -1;
    std::string name;
};

class Camera;
class Display;

class FaceRecognition {
public:
    FaceRecognition(Camera* camera, Display* display);

    void ProcessOneFrame(QueuedFrame* qframe);
    void DrawOverlay(uint8_t* rgb565, uint16_t w, uint16_t h);

    void SetExplainOnlyWhenAwake(std::function<bool()> fn) { explain_only_when_awake_ = std::move(fn); }
    void SetTriggerWakeForExplain(std::function<void()> fn) { trigger_wake_for_explain_ = std::move(fn); }
    void SetOnNewFaceRegistered(std::function<void(const std::string& name)> fn) { on_new_face_registered_ = std::move(fn); }

private:
    Camera* camera_ = nullptr;
    Display* display_ = nullptr;

    std::vector<FaceRecord> faces_;

    bool is_remote_recognition_in_progress_ = false;
    int64_t remote_recognition_start_ms_ = 0;
    int64_t last_explain_time_ms_ = 0;
    bool last_explain_resulted_in_registration_ = false;
    std::function<bool()> explain_only_when_awake_;
    std::function<void()> trigger_wake_for_explain_;
    std::function<void(const std::string& name)> on_new_face_registered_;

    void* esp_dl_detect_handle_ = nullptr;

    std::vector<FaceBox> last_detection_boxes_;

    void InitializeIfNeeded();

    void SimulateLocalDetection(std::string& out_person_name);
    std::string SimulateRemoteRecognition();

    bool UpdateLocalDatabase(const std::string& person_name);
    int FindFaceIndexByName(const std::string& name) const;
    int AllocateFaceSlot() const;
    std::string AllocateUnknownName() const;

    void ShowResultOnDisplay(const std::string& message);
    void LogRegisteredList() const;

    static int64_t GetTimeMs();
};
