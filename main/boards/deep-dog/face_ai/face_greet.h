#pragma once

#include <cstdint>
#include <functional>
#include <string>

/** 当前对话者 / 主动招呼（P02） */

enum class DeepDogFaceGreetSource : uint8_t {
    None = 0,
    Recognition = 1,
    Simulate = 2,
};

struct DeepDogFaceSpeaker {
    int local_id = 0;
    char display_name[32] = {};
    char immich_person_id[40] = {};
    uint32_t since = 0;
    DeepDogFaceGreetSource source = DeepDogFaceGreetSource::None;
    bool greet_pending = false;
    bool present = false;
};

void DeepDogFaceGreetInit();
void DeepDogFaceGreetSetNotifyCallback(std::function<void()> cb);

bool DeepDogFaceGreetIsEnabled();
int DeepDogFaceGreetGetGapSec();
bool DeepDogFaceGreetSetConfig(bool enabled, int gap_sec);

void DeepDogFaceGreetSetSpeaker(int local_id, const char* name, const char* immich_person_id,
                                DeepDogFaceGreetSource src, bool greet_pending);
void DeepDogFaceGreetClearSpeaker();
DeepDogFaceSpeaker DeepDogFaceGreetGetSpeaker();

bool DeepDogFaceGreetIsConfirmedName(int local_id, const char* name);

/** 识别命中前调用（last_seen_before 为 TouchLastSeen 之前的值） */
bool DeepDogFaceGreetMaybeFromRecognition(int local_id, const char* display_name,
                                          const char* immich_person_id, uint32_t last_seen_before);

/** MQTT/MCP 联调：注入身份并唤醒 */
bool DeepDogFaceGreetSimulateAndWake(const char* name, int local_id);

void DeepDogFaceGreetOnPrimaryFace(int local_id);
void DeepDogFaceGreetOnNoFace();

std::string DeepDogFaceGreetFormatIdentityJson();
const char* DeepDogFaceGreetSourceStr(DeepDogFaceGreetSource s);
