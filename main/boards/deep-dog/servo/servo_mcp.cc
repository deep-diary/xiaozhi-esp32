#include "servo/servo_control.h"

#include "mcp_server.h"

#include <cJSON.h>
#include <esp_log.h>

#include <string>
#include <vector>

#define TAG "dog_servo_mcp"

void RegisterServoMcpTools(McpServer& mcp_server) {
#if !DEEP_DOG_SERVO_ENABLE
    (void)mcp_server;
    return;
#else
    mcp_server.AddTool(
        "self.servo.set_angle",
        "设置舵机角度；index=0/1；duration_ms=0立即到位",
        PropertyList(std::vector<Property>{
            Property("index", kPropertyTypeInteger, 0, 0, DEEP_DOG_SERVO_COUNT - 1),
            Property("angle", kPropertyTypeInteger, 90, 0, 360),
            Property("duration_ms", kPropertyTypeInteger, 0, 0, 30000),
        }),
        [](const PropertyList& props) -> ReturnValue {
            const int index = props["index"].value<int>();
            const int angle = props["angle"].value<int>();
            const int duration_ms = props["duration_ms"].value<int>();
            const esp_err_t err =
                DeepDogServoSetAngle(index, angle, static_cast<uint32_t>(duration_ms < 0 ? 0 : duration_ms));
            if (err != ESP_OK) {
                return std::string("舵机设置失败");
            }
            return std::string("舵机") + std::to_string(index) + " -> " + std::to_string(angle) + "°";
        });

    mcp_server.AddTool(
        "self.servo.set_type",
        "设置舵机类型 90/180/270/360",
        PropertyList(std::vector<Property>{
            Property("index", kPropertyTypeInteger, 0, 0, DEEP_DOG_SERVO_COUNT - 1),
            Property("type", kPropertyTypeInteger, 180, 90, 360),
        }),
        [](const PropertyList& props) -> ReturnValue {
            const int index = props["index"].value<int>();
            const int type_v = props["type"].value<int>();
            servo_type_t type = SERVO_TYPE_180;
            switch (type_v) {
                case 90:
                    type = SERVO_TYPE_90;
                    break;
                case 270:
                    type = SERVO_TYPE_270;
                    break;
                case 360:
                    type = SERVO_TYPE_360;
                    break;
                case 180:
                    type = SERVO_TYPE_180;
                    break;
                default:
                    return std::string("类型仅支持 90/180/270/360");
            }
            if (DeepDogServoSetType(index, type) != ESP_OK) {
                return std::string("设置类型失败");
            }
            return std::string("舵机") + std::to_string(index) + " 类型=" + std::to_string(type_v);
        });

    mcp_server.AddTool(
        "self.servo.attach",
        "绑定舵机 PWM；type=90/180/270/360",
        PropertyList(std::vector<Property>{
            Property("index", kPropertyTypeInteger, 0, 0, DEEP_DOG_SERVO_COUNT - 1),
            Property("type", kPropertyTypeInteger, 180, 90, 360),
        }),
        [](const PropertyList& props) -> ReturnValue {
            const int index = props["index"].value<int>();
            const int type_v = props["type"].value<int>();
            servo_type_t type = SERVO_TYPE_180;
            if (type_v == 90) {
                type = SERVO_TYPE_90;
            } else if (type_v == 270) {
                type = SERVO_TYPE_270;
            } else if (type_v == 360) {
                type = SERVO_TYPE_360;
            } else if (type_v != 180) {
                return std::string("类型仅支持 90/180/270/360");
            }
            if (DeepDogServoAttach(index, type) != ESP_OK) {
                return std::string("attach 失败");
            }
            return std::string("舵机") + std::to_string(index) + " 已绑定";
        });

    mcp_server.AddTool(
        "self.servo.detach",
        "释放舵机 PWM",
        PropertyList(std::vector<Property>{
            Property("index", kPropertyTypeInteger, 0, 0, DEEP_DOG_SERVO_COUNT - 1),
        }),
        [](const PropertyList& props) -> ReturnValue {
            const int index = props["index"].value<int>();
            if (DeepDogServoDetach(index) != ESP_OK) {
                return std::string("detach 失败");
            }
            return std::string("舵机") + std::to_string(index) + " 已释放";
        });

    mcp_server.AddTool("self.servo.get_status", "读取两路舵机状态", PropertyList(),
                       [](const PropertyList&) -> ReturnValue {
                           cJSON* root = cJSON_CreateObject();
                           cJSON* arr = cJSON_AddArrayToObject(root, "servos");
                           for (int i = 0; i < DeepDogServoCount(); ++i) {
                               DeepDogServoSnapshot snap {};
                               if (!DeepDogServoGetSnapshot(i, &snap)) {
                                   continue;
                               }
                               cJSON* item = cJSON_CreateObject();
                               cJSON_AddNumberToObject(item, "index", snap.index);
                               cJSON_AddNumberToObject(item, "angle", snap.angle);
                               cJSON_AddNumberToObject(item, "target", snap.target);
                               cJSON_AddBoolToObject(item, "attached", snap.attached);
                               cJSON_AddBoolToObject(item, "moving", snap.moving);
                               cJSON_AddNumberToObject(item, "type", snap.type);
                               cJSON_AddNumberToObject(item, "min", snap.min_angle);
                               cJSON_AddNumberToObject(item, "max", snap.max_angle);
                               cJSON_AddItemToArray(arr, item);
                           }
                           char* raw = cJSON_PrintUnformatted(root);
                           std::string out = raw ? raw : "{}";
                           if (raw) {
                               cJSON_free(raw);
                           }
                           cJSON_Delete(root);
                           return out;
                       });

    ESP_LOGI(TAG, "MCP servo tools registered");
#endif
}
