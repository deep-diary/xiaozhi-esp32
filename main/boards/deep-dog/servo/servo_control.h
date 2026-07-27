#pragma once

#include "servo/Servo.h"

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

#define DEEP_DOG_SERVO_COUNT 2

typedef struct {
    int index;
    int angle;
    int target;
    int min_angle;
    int max_angle;
    int type;  // 90/180/270/360
    bool attached;
    bool moving;
} DeepDogServoSnapshot;

typedef void (*DeepDogServoBankNotifyCb)(void* ctx);

esp_err_t DeepDogServoInit(void);
void DeepDogServoDeinit(void);
bool DeepDogServoReady(void);

esp_err_t DeepDogServoSetAngle(int index, int angle, uint32_t duration_ms);
esp_err_t DeepDogServoSetType(int index, servo_type_t type);
esp_err_t DeepDogServoAttach(int index, servo_type_t type);
esp_err_t DeepDogServoDetach(int index);
bool DeepDogServoGetSnapshot(int index, DeepDogServoSnapshot* out);
int DeepDogServoCount(void);

void DeepDogServoSetNotifyCallback(DeepDogServoBankNotifyCb cb, void* ctx);

#ifdef __cplusplus
}

void RegisterServoMcpTools(class McpServer& mcp_server);

#endif
