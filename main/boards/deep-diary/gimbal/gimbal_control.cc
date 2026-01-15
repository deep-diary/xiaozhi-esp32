#include "gimbal_control.h"
#include <esp_log.h>

#define TAG "GimbalControl"

GimbalControl::GimbalControl(Gimbal_t* gimbal, McpServer& mcp_server) 
    : gimbal_(gimbal) {
    
    // 舵机控制工具
    mcp_server.AddTool("self.gimbal.move_up", "云台向上移动", PropertyList(std::vector<Property>{
        Property("angle", kPropertyTypeInteger, 10, 1, 180)
    }), [this](const PropertyList& properties) -> ReturnValue {
        int angle = properties["angle"].value<int>();
        Gimbal_moveUp(gimbal_, angle);
        ESP_LOGI(TAG, "云台向上移动 %d 度", angle);
        return true;
    });

    mcp_server.AddTool("self.gimbal.move_down", "云台向下移动", PropertyList(std::vector<Property>{
        Property("angle", kPropertyTypeInteger, 10, 1, 180)
    }), [this](const PropertyList& properties) -> ReturnValue {
        int angle = properties["angle"].value<int>();
        Gimbal_moveDown(gimbal_, angle);
        ESP_LOGI(TAG, "云台向下移动 %d 度", angle);
        return true;
    });

    mcp_server.AddTool("self.gimbal.move_left", "云台向左移动", PropertyList(std::vector<Property>{
        Property("angle", kPropertyTypeInteger, 10, 1, 270)
    }), [this](const PropertyList& properties) -> ReturnValue {
        int angle = properties["angle"].value<int>();
        Gimbal_moveLeft(gimbal_, angle);
        ESP_LOGI(TAG, "云台向左移动 %d 度", angle);
        return true;
    });

    mcp_server.AddTool("self.gimbal.move_right", "云台向右移动", PropertyList(std::vector<Property>{
        Property("angle", kPropertyTypeInteger, 10, 1, 270)
    }), [this](const PropertyList& properties) -> ReturnValue {
        int angle = properties["angle"].value<int>();
        Gimbal_moveRight(gimbal_, angle);
        ESP_LOGI(TAG, "云台向右移动 %d 度", angle);
        return true;
    });

    mcp_server.AddTool("self.gimbal.set_angles", "设置云台角度", PropertyList(std::vector<Property>{
        Property("pan_angle", kPropertyTypeInteger, 135, 0, 270),
        Property("tilt_angle", kPropertyTypeInteger, 90, 0, 180)
    }), [this](const PropertyList& properties) -> ReturnValue {
        int pan_angle = properties["pan_angle"].value<int>();
        int tilt_angle = properties["tilt_angle"].value<int>();
        Gimbal_setAngles(gimbal_, pan_angle, tilt_angle);
        ESP_LOGI(TAG, "设置云台角度 - PAN: %d°, TILT: %d°", pan_angle, tilt_angle);
        return true;
    });
}
