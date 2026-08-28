#include "config.h"
/* deep-dog feature gate: whole-file */
#if DEEP_DOG_MOTOR_ENABLE

#include "deep_motor_control.h"
#include "protocol_motor.h"
#include "motor_config.h"
#include "config.h"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "DeepMotorControl"

static void RegisterMotorMcpToolsImpl(McpServer& mcp_server, DeepMotor* deep_motor) {
    // MOT-14：电机工具统一 self.motor.* 前缀。
    // 粘性默认电机：motor_id=0（或省略）= 当前活跃（默认）电机；显式 motor_id>0 的指令在注册成功后
    // 自动把该电机置为活跃电机，后续省略编号即作用于它。解析失败返回 -1，提示写入 err。
    auto resolve_motor_id = [deep_motor](int raw_id, std::string& err) -> int {
        if (!deep_motor) {
            err = "深度电机管理器未初始化";
            return -1;
        }
        if (raw_id == 0) {
            const int8_t active = deep_motor->getActiveMotorId();
            if (active <= 0) {
                err = "未指定电机编号且当前无活跃电机，请先指定电机编号";
                return -1;
            }
            return static_cast<int>(active);
        }
        const uint8_t mid = static_cast<uint8_t>(raw_id);
        if (!deep_motor->isMotorRegistered(mid) && !deep_motor->registerMotor(mid)) {
            err = "电机ID " + std::to_string(raw_id) + " 注册失败（槽位已满或错误）";
            return -1;
        }
        deep_motor->setActiveMotorId(mid);
        return raw_id;
    };

    // ========== 总线扫描 / 注册 / 发现 ==========

    // CAN 总线电机 ID 扫描（通信类型 0，异步发现）
    mcp_server.AddTool("self.motor.scan_bus", "扫描 CAN 总线电机 ID（通信类型 0 探测，异步注册）",
                        PropertyList(), [deep_motor](const PropertyList&) -> ReturnValue {
        if (!deep_motor) {
            return std::string("DeepMotor 未初始化");
        }
        deep_motor->sendBusScanProbes();
        ESP_LOGI(TAG, "已发送 ID 1–127 探测帧");
        return std::string("已发送 ID 1–127 探测帧，应答由 CAN RX 异步注册");
    });

    // 获取所有已注册电机ID
    mcp_server.AddTool("self.motor.list", "获取所有已注册电机ID", PropertyList(), [deep_motor](const PropertyList&) -> ReturnValue {
        if (!deep_motor) {
            ESP_LOGW(TAG, "深度电机管理器未初始化");
            return std::string("深度电机管理器未初始化");
        }

        int8_t motor_ids[MAX_MOTOR_COUNT];
        uint8_t count = deep_motor->getRegisteredMotorIds(motor_ids, MAX_MOTOR_COUNT);

        ESP_LOGI(TAG, "已注册电机数量: %d", count);
        for (int i = 0; i < count; i++) {
            ESP_LOGI(TAG, "电机ID: %d", motor_ids[i]);
        }

        std::string result = "已注册电机数量: " + std::to_string(count) + "\n";
        if (count > 0) {
            result += "电机ID列表: ";
            for (int i = 0; i < count; i++) {
                if (i > 0) result += ", ";
                result += std::to_string(motor_ids[i]);
            }
        } else {
            result += "暂无已注册的电机";
        }

        return result;
    });

    // 显式设置默认（活跃）电机；后续省略 motor_id 的指令默认作用于该电机
    mcp_server.AddTool("self.motor.set_active",
        "设置默认（活跃）电机；后续省略 motor_id（传 0）的指令默认作用于该电机。会自动注册该编号。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 1, 1, 255)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }
        ESP_LOGI(TAG, "设置默认（活跃）电机ID为: %d", motor_id);
        return std::string("成功设置默认电机ID为: " + std::to_string(motor_id) +
                          "，后续省略编号的指令将作用于该电机");
    });

    // ========== 运动控制（MIT / 位置 / 速度） ==========

    // 位置模式：position 整数 ÷100 = rad
    mcp_server.AddTool("self.motor.set_position",
        "设置电机目标位置（位置模式）。position 整数÷100=弧度（如 2rad 填 200）。"
        "motor_id=0 或省略时作用于当前活跃电机（最近一次显式指定的电机）。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255),
        Property("position", kPropertyTypeInteger, 200, -314, 314)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }
        const float position = properties["position"].value<int>() / 100.0f;

        if (deep_motor->setMotorPosition(static_cast<uint8_t>(motor_id), position, 10.0f)) {
            ESP_LOGI(TAG, "发送电机位置控制指令成功 - 电机ID: %d, 位置: %.2f 弧度", motor_id, position);
            return std::string("电机ID " + std::to_string(motor_id) + " 位置设置成功: " + std::to_string(position) + " 弧度");
        }
        ESP_LOGE(TAG, "发送电机位置控制指令失败 - 电机ID: %d", motor_id);
        return std::string("电机ID " + std::to_string(motor_id) + " 位置设置失败");
    });

    // 速度模式：speed 整数 ÷10 = rad/s
    mcp_server.AddTool("self.motor.set_speed",
        "设置电机目标角速度（速度模式）。speed 整数÷10=rad/s（如 2.0rad/s 填 20）。"
        "motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255),
        Property("speed", kPropertyTypeInteger, 0, -300, 300)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }
        const float speed = properties["speed"].value<int>() / 10.0f;

        if (MotorProtocol::setSpeedRef(motor_id, speed)) {
            if (deep_motor) {
                (void)deep_motor->setMotorSpeedRef(static_cast<uint8_t>(motor_id), speed);
            }
            ESP_LOGI(TAG, "发送电机速度指令成功 - 电机ID: %d, 速度: %.1f rad/s", motor_id, speed);
            return true;
        }
        ESP_LOGE(TAG, "发送电机速度控制指令失败 - 电机ID: %d", motor_id);
        return false;
    });

    // 位置/角速/kp/kd/τ：一律「参数÷10 = 物理量」，与语音整数对齐
    mcp_server.AddTool(
        "self.motor.control_mit",
        "MIT 运控（先注册）。position_x10 等参数=物理量×10，设备上再÷10；不是用户念的数字原样放大。"
        "例：目标 2rad 必须填 20；1rad→10；勿把「2弧度」填成 200（会超 ±125）。角速 2rad/s→20；kp=0.5→5；kd=0.5→5。"
        "默认：除扭矩外均为物理量 1.0（position/velocity/kp/kd 的 _x10 默认 10，tau_ff 默认 0）。"
        "position_x10 ±125；velocity_x10 ±500（÷10 后 ±50rad/s）；kp_x10 0～5000→kp 0～500；kd_x10 0～50→kd 0～5。"
        "motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
            Property("motor_id", kPropertyTypeInteger, 0, 0, 255),
            Property("position_x10", kPropertyTypeInteger, 10, -125, 125),
            Property("velocity_x10", kPropertyTypeInteger, 10, -500, 500),
            Property("kp_x10", kPropertyTypeInteger, 10, 0, 5000),
            Property("kd_x10", kPropertyTypeInteger, 10, 0, 50),
            Property("tau_ff_x10", kPropertyTypeInteger, 0, -60, 60),
        }),
        [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
            std::string err;
            const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
            if (motor_id < 0) {
                return err;
            }
            const int pos_raw = properties["position_x10"].value<int>();
            const float position = static_cast<float>(pos_raw) / 10.0f;
            const float velocity = properties["velocity_x10"].value<int>() / 10.0f;
            const float kp = properties["kp_x10"].value<int>() / 10.0f;
            const float kd = properties["kd_x10"].value<int>() / 10.0f;
            const float tau_ff = properties["tau_ff_x10"].value<int>() / 10.0f;

            if (!deep_motor) {
                return std::string("DeepMotor 未初始化");
            }
            const uint8_t mid = static_cast<uint8_t>(motor_id);
            if (deep_motor->setMotorMitCommand(mid, position, velocity, kp, kd, tau_ff)) {
                ESP_LOGI(TAG, "MIT 下发成功 id=%d pos_x10=%d -> p=%.3f rad v=%.3f kp=%.2f kd=%.2f", motor_id, pos_raw,
                         position, velocity, kp, kd);
                return std::string("MIT 下发成功: id=" + std::to_string(motor_id));
            }
            ESP_LOGE(TAG, "MIT 下发失败 id=%d", motor_id);
            return std::string("MIT 下发失败 id=" + std::to_string(motor_id));
        });

    // ========== ③ 初始化 / 使能 ==========

    // 电机使能（仅 CAN 使能，不切换工作模式）
    mcp_server.AddTool("self.motor.enable",
        "激活/使能电机（仅 CAN 使能，不切换工作模式）。motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }
        const uint8_t mid = static_cast<uint8_t>(motor_id);

        if (!deep_motor) {
            if (MotorProtocol::enableMotor(motor_id)) {
                ESP_LOGI(TAG, "电机使能成功 - 电机ID: %d", motor_id);
                return true;
            }
            ESP_LOGE(TAG, "电机使能失败 - 电机ID: %d", motor_id);
            return false;
        }

        if (deep_motor->ensureMotorEnabled(mid)) {
            ESP_LOGI(TAG, "电机使能成功 - 电机ID: %d", motor_id);
            return true;
        }
        ESP_LOGE(TAG, "电机使能失败 - 电机ID: %d", motor_id);
        return false;
    });

    // 电机停止/失能
    mcp_server.AddTool("self.motor.reset",
        "停止/失能电机（reset）。motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }
        const uint8_t mid = static_cast<uint8_t>(motor_id);

        if (MotorProtocol::resetMotor(motor_id)) {
            if (deep_motor) {
                deep_motor->invalidateMotorCommandCache(mid);
                deep_motor->resetMotorInitState(mid);
            }
            ESP_LOGI(TAG, "电机停止成功 - 电机ID: %d", motor_id);
            return true;
        }
        ESP_LOGE(TAG, "电机停止失败 - 电机ID: %d", motor_id);
        return false;
    });

    // ========== ② 状态查询 ==========

    // 电机状态查询
    mcp_server.AddTool("self.motor.get_status",
        "获取电机状态（角度/速度/扭矩/温度）。motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }

        motor_status_t status;
        if (deep_motor->getMotorStatus(motor_id, &status)) {
            std::string result = "电机ID " + std::to_string(motor_id) + " 状态:\n";
            result += "  角度: " + std::to_string(status.current_angle) + " rad\n";
            result += "  速度: " + std::to_string(status.current_speed) + " rad/s\n";
            result += "  扭矩: " + std::to_string(status.current_torque) + " N·m\n";
            result += "  温度: " + std::to_string(status.current_temp) + "°C";
            
            ESP_LOGI(TAG, "电机ID %d 状态: 角度=%.3f rad, 速度=%.3f rad/s, 扭矩=%.3f N·m, 温度=%.1f°C", 
                     motor_id, status.current_angle, status.current_speed, 
                     status.current_torque, status.current_temp);
            return result;
        }
        return std::string("获取电机ID " + std::to_string(motor_id) + " 状态失败");
    });

    // 打印所有电机状态（调试，输出到日志）
    mcp_server.AddTool("self.motor.print_all", "打印所有电机状态到日志（调试用）", PropertyList(), [deep_motor](const PropertyList&) -> ReturnValue {
        if (!deep_motor) {
            ESP_LOGW(TAG, "深度电机管理器未初始化");
            return std::string("深度电机管理器未初始化");
        }

        deep_motor->printAllMotorStatus();
        return std::string("已打印所有电机状态到日志");
    });

    // ========== ④ 模式切换 ==========
    // 模式工具名自描述（语音可靠）；motor_id=0 作用于活跃电机。

    mcp_server.AddTool("self.motor.set_control_mode",
        "设置电机为运控（MIT）模式。motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }

        if (MotorProtocol::setMotorControlMode(motor_id)) {
            deep_motor->markMotorRunMode(static_cast<uint8_t>(motor_id), MOTOR_CTRL_MODE);
            ESP_LOGI(TAG, "设置电机%d为运控模式成功", motor_id);
            return std::string("设置电机" + std::to_string(motor_id) + "为运控模式成功");
        }
        ESP_LOGE(TAG, "设置电机%d为运控模式失败", motor_id);
        return std::string("设置电机" + std::to_string(motor_id) + "为运控模式失败");
    });

    mcp_server.AddTool("self.motor.set_position_mode",
        "设置电机为位置模式。motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }

        if (MotorProtocol::setMotorPositionMode(motor_id)) {
            if (deep_motor) {
                deep_motor->markMotorRunMode(static_cast<uint8_t>(motor_id), MOTOR_POS_MODE);
            }
            ESP_LOGI(TAG, "设置电机%d为位置模式成功", motor_id);
            return std::string("设置电机" + std::to_string(motor_id) + "为位置模式成功");
        }
        ESP_LOGE(TAG, "设置电机%d为位置模式失败", motor_id);
        return std::string("设置电机" + std::to_string(motor_id) + "为位置模式失败");
    });

    mcp_server.AddTool("self.motor.set_speed_mode",
        "设置电机为速度模式。motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }

        if (MotorProtocol::setMotorSpeedMode(motor_id)) {
            if (deep_motor) {
                deep_motor->markMotorRunMode(static_cast<uint8_t>(motor_id), MOTOR_SPEED_MODE);
            }
            ESP_LOGI(TAG, "设置电机%d为速度模式成功", motor_id);
            return std::string("设置电机" + std::to_string(motor_id) + "为速度模式成功");
        }
        ESP_LOGE(TAG, "设置电机%d为速度模式失败", motor_id);
        return std::string("设置电机" + std::to_string(motor_id) + "为速度模式失败");
    });

    mcp_server.AddTool("self.motor.set_current_mode",
        "设置电机为电流（力矩）模式。motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }

        if (MotorProtocol::setMotorCurrentMode(motor_id)) {
            if (deep_motor) {
                deep_motor->markMotorRunMode(static_cast<uint8_t>(motor_id), MOTOR_CURRENT_MODE);
            }
            ESP_LOGI(TAG, "设置电机%d为电流模式成功", motor_id);
            return std::string("设置电机" + std::to_string(motor_id) + "为电流模式成功");
        }
        ESP_LOGE(TAG, "设置电机%d为电流模式失败", motor_id);
        return std::string("设置电机" + std::to_string(motor_id) + "为电流模式失败");
    });

    mcp_server.AddTool("self.motor.set_zero_position",
        "设置电机当前位置为零位。motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }

        if (MotorProtocol::setMotorZero(motor_id)) {
            ESP_LOGI(TAG, "设置电机%d零位成功", motor_id);
            return std::string("设置电机" + std::to_string(motor_id) + "零位成功");
        }
        ESP_LOGE(TAG, "设置电机%d零位失败", motor_id);
        return std::string("设置电机" + std::to_string(motor_id) + "零位失败");
    });

    // ========== ③ 初始化 ==========

    // 初始化（按编译配置选择 MIT/位置模式）
    mcp_server.AddTool("self.motor.initialize",
        "初始化电机（reset、写零、切模式、使能）。max_speed÷10=rad/s 作为目标速度意图参数。"
        "motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255),
        // 默认 10 → 1.0 rad/s，须在 [10,500] 内（÷10 后 ≤50 rad/s），否则 Property 构造抛异常会导致整机 abort 重启
        Property("max_speed", kPropertyTypeInteger, 10, 10, 500)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }
        const int max_speed_int = properties["max_speed"].value<int>();
        const float target_velocity_rad_s = max_speed_int / 10.0f;
        if (!deep_motor) {
            return std::string("DeepMotor 未初始化");
        }
        const uint8_t mid = static_cast<uint8_t>(motor_id);
        if (deep_motor->initializeMotor(mid, target_velocity_rad_s)) {
#if DEEP_DOG_MOTOR_INIT_ASYNC
            ESP_LOGI(TAG, "电机%d 异步初始化已启动 target_velocity=%.2f rad/s", motor_id, target_velocity_rad_s);
            return std::string("电机 " + std::to_string(motor_id) + " 异步初始化已启动，等待 motor/status init_state=ready");
#else
            ESP_LOGI(TAG, "电机%d 初始化成功（带反馈校验），target_velocity=%.2f rad/s", motor_id, target_velocity_rad_s);
            return std::string("电机 " + std::to_string(motor_id) + " 初始化成功，target_velocity=" +
                   std::to_string(target_velocity_rad_s) + " rad/s");
#endif
        }
        ESP_LOGE(TAG, "电机%d MIT 初始化失败", motor_id);
        return std::string("电机 " + std::to_string(motor_id) + " 初始化失败");
    });

    // ========== 状态查询任务 ==========

    // 启动状态查询任务
    mcp_server.AddTool("self.motor.start_status_task",
        "启动电机周期状态查询任务。motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }

        if (deep_motor->startInitStatusTask(static_cast<uint8_t>(motor_id))) {
            ESP_LOGI(TAG, "启动电机%d状态查询任务成功", motor_id);
            return std::string("启动电机" + std::to_string(motor_id) + "状态查询任务成功");
        }
        ESP_LOGE(TAG, "启动电机%d状态查询任务失败", motor_id);
        return std::string("启动电机" + std::to_string(motor_id) + "状态查询任务失败");
    });

    // 停止状态查询任务
    mcp_server.AddTool("self.motor.stop_status_task", "停止电机状态查询任务", PropertyList(), [deep_motor](const PropertyList&) -> ReturnValue {
        if (!deep_motor) {
            ESP_LOGW(TAG, "深度电机管理器未初始化");
            return std::string("深度电机管理器未初始化");
        }
        
        if (deep_motor->stopInitStatusTask()) {
            ESP_LOGI(TAG, "停止电机状态查询任务成功");
            return std::string("停止电机状态查询任务成功");
        } else {
            ESP_LOGW(TAG, "停止电机状态查询任务失败");
            return std::string("停止电机状态查询任务失败");
        }
    });

    // ========== ⑥ 示教录制 ==========

    // 开始录制
    mcp_server.AddTool("self.motor.start_recording",
        "开始示教录制模式（停电机后可拖动采样）。motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }

        if (deep_motor->startTeaching(static_cast<uint8_t>(motor_id))) {
            ESP_LOGI(TAG, "开始录制模式成功，电机ID: %d", motor_id);
            return std::string("开始录制模式成功，电机ID: " + std::to_string(motor_id) + "，可以开始拖动电机");
        }
        ESP_LOGE(TAG, "开始录制模式失败，电机ID: %d", motor_id);
        return std::string("开始录制模式失败，电机ID: " + std::to_string(motor_id));
    });

    // 结束录制
    mcp_server.AddTool("self.motor.stop_recording", "结束录制模式", PropertyList(), [deep_motor](const PropertyList&) -> ReturnValue {
        if (!deep_motor) {
            ESP_LOGW(TAG, "深度电机管理器未初始化");
            return std::string("深度电机管理器未初始化");
        }
        
        if (deep_motor->stopTeaching()) {
            uint16_t point_count = deep_motor->getTeachingPointCount();
            ESP_LOGI(TAG, "结束录制模式成功，共记录 %d 个录制点", point_count);
            return std::string("结束录制模式成功，共记录 " + std::to_string(point_count) + " 个录制点");
        } else {
            ESP_LOGE(TAG, "结束录制模式失败");
            return std::string("结束录制模式失败");
        }
    });

    // 播放录制（MIT 轨迹，MOT-11）
    mcp_server.AddTool("self.motor.play_recording",
                       "播放录制（MIT 运控：过渡→轨迹插值，默认 10s）。motor_id=0 或省略时作用于当前活跃电机。",
                       PropertyList(std::vector<Property>{
                           Property("motor_id", kPropertyTypeInteger, 0, 0, 255),
                           Property("duration_ms", kPropertyTypeInteger, TEACHING_PLAY_DURATION_MS_DEFAULT, 1000,
                                    120000),
                           Property("blend_ms", kPropertyTypeInteger, TEACHING_PLAY_BLEND_MS_DEFAULT, 0, 5000),
                           Property("time_scale_x10", kPropertyTypeInteger, 10, 1, 100),
                           Property("kp_x10", kPropertyTypeInteger, 10, 0, 5000),
                           Property("kd_x10", kPropertyTypeInteger, 10, 0, 50),
                           Property("tau_ff_x10", kPropertyTypeInteger, 0, -60, 60),
                       }),
                       [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }

        TeachingPlayConfig cfg;
        cfg.duration_ms = static_cast<uint32_t>(properties["duration_ms"].value<int>());
        cfg.blend_ms = static_cast<uint32_t>(properties["blend_ms"].value<int>());
        cfg.time_scale = properties["time_scale_x10"].value<int>() / 10.0f;
        cfg.kp = properties["kp_x10"].value<int>() / 10.0f;
        cfg.kd = properties["kd_x10"].value<int>() / 10.0f;
        cfg.tau_ff = properties["tau_ff_x10"].value<int>() / 10.0f;

        if (deep_motor->executeTeaching(static_cast<uint8_t>(motor_id), &cfg)) {
            const uint16_t point_count = deep_motor->getTeachingPointCount();
            ESP_LOGI(TAG, "MIT 播放启动 id=%d 点数=%u duration=%ums", motor_id, point_count,
                     (unsigned)cfg.duration_ms);
            return std::string("MIT 播放启动，电机ID: " + std::to_string(motor_id) + "，总点数: " +
                               std::to_string(point_count) + "，duration_ms=" + std::to_string(cfg.duration_ms));
        }
        ESP_LOGE(TAG, "MIT 播放失败 id=%d", motor_id);
        return std::string("播放录制失败，电机ID: " + std::to_string(motor_id));
    });

    // 获取录制状态
    mcp_server.AddTool("self.motor.get_recording_status", "获取录制状态", PropertyList(), [deep_motor](const PropertyList&) -> ReturnValue {
        if (!deep_motor) {
            ESP_LOGW(TAG, "深度电机管理器未初始化");
            return std::string("深度电机管理器未初始化");
        }
        
        bool is_teaching = deep_motor->isTeachingMode();
        bool data_ready = deep_motor->isTeachingDataReady();
        uint16_t point_count = deep_motor->getTeachingPointCount();
        
        std::string result = "录制状态:\n";
        result += "  录制模式: " + std::string(is_teaching ? "进行中" : "未启动") + "\n";
        result += "  数据就绪: " + std::string(data_ready ? "是" : "否") + "\n";
        result += "  录制点数: " + std::to_string(point_count);
        
        ESP_LOGI(TAG, "录制状态 - 模式: %s, 数据就绪: %s, 点数: %d", 
                 is_teaching ? "进行中" : "未启动", 
                 data_ready ? "是" : "否", 
                 point_count);
        
        return result;
    });

    // ========== ⑦ 调试信号（厂内正弦测试） ==========

    mcp_server.AddTool("self.motor.start_sin_signal",
        "开始电机正弦测试信号（厂内调试）。amplitude÷100=幅度(rad)，frequency÷10=频率(Hz)。"
        "motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255),
        Property("amplitude", kPropertyTypeInteger, 200, 1, 1000),
        Property("frequency", kPropertyTypeInteger, 20, 1, 100)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }
        const float amplitude = properties["amplitude"].value<int>() / 100.0f;
        const float frequency = properties["frequency"].value<int>() / 10.0f;

        if (MotorProtocol::startSinSignal(motor_id, amplitude, frequency)) {
            ESP_LOGI(TAG, "开始正弦信号成功 - 电机ID: %d, 幅度: %.2f, 频率: %.1f Hz", motor_id, amplitude, frequency);
            return std::string("开始正弦信号成功 - 电机ID: " + std::to_string(motor_id) +
                             ", 幅度: " + std::to_string(amplitude) +
                             ", 频率: " + std::to_string(frequency) + " Hz");
        }
        ESP_LOGE(TAG, "开始正弦信号失败 - 电机ID: %d", motor_id);
        return std::string("开始正弦信号失败 - 电机ID: " + std::to_string(motor_id));
    });

    mcp_server.AddTool("self.motor.stop_sin_signal",
        "停止电机正弦测试信号。motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }

        if (MotorProtocol::stopSinSignal(motor_id)) {
            ESP_LOGI(TAG, "停止正弦信号成功 - 电机ID: %d", motor_id);
            return std::string("停止正弦信号成功 - 电机ID: " + std::to_string(motor_id));
        }
        ESP_LOGE(TAG, "停止正弦信号失败 - 电机ID: %d", motor_id);
        return std::string("停止正弦信号失败 - 电机ID: " + std::to_string(motor_id));
    });

    // ========== 软件版本查询 ==========

    mcp_server.AddTool("self.motor.get_software_version",
        "获取电机软件版本号。motor_id=0 或省略时作用于当前活跃电机。",
        PropertyList(std::vector<Property>{
        Property("motor_id", kPropertyTypeInteger, 0, 0, 255)
    }), [deep_motor, resolve_motor_id](const PropertyList& properties) -> ReturnValue {
        std::string err;
        const int motor_id = resolve_motor_id(properties["motor_id"].value<int>(), err);
        if (motor_id < 0) {
            return err;
        }

        const uint8_t mid = static_cast<uint8_t>(motor_id);
        (void)deep_motor->requestMotorSoftwareVersion(mid);
        vTaskDelay(pdMS_TO_TICKS(80));

        char version[16];
        if (deep_motor->getMotorSoftwareVersion(mid, version, sizeof(version))) {
            std::string result = "电机ID " + std::to_string(motor_id) + " 软件版本: " + std::string(version);
            ESP_LOGI(TAG, "获取电机ID %d 软件版本成功: %s", motor_id, version);
            return result;
        }
        ESP_LOGW(TAG, "获取电机ID %d 软件版本失败", motor_id);
        return std::string("获取电机ID " + std::to_string(motor_id) + " 软件版本失败");
    });

    ESP_LOGI(TAG, "深度电机控制MCP工具注册完成（self.motor.* 统一前缀，含粘性默认电机；MOT-14）");
}

void RegisterMotorMcpTools(McpServer& mcp_server, DeepMotor* deep_motor) {
#if !DEEP_DOG_MOTOR_ENABLE
    (void)mcp_server;
    (void)deep_motor;
#else
    RegisterMotorMcpToolsImpl(mcp_server, deep_motor);
#endif
}

DeepMotorControl::DeepMotorControl(DeepMotor* deep_motor, McpServer& mcp_server)
    : deep_motor_(deep_motor) {
    RegisterMotorMcpTools(mcp_server, deep_motor);
}

#endif  // DEEP_DOG_MOTOR_ENABLE
