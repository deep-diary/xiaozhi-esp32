#include "config.h"

#if DEEP_DOG_MICROROS_ENABLE

#include "microros/microros_client.h"
#include "microros/microros_config.h"

#include <cstring>
#include <stdio.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <rcl/error_handling.h>
#include <rcl/rcl.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>
#include <rmw_microros/rmw_microros.h>
#include <micro_ros_utilities/string_utilities.h>
#include <micro_ros_utilities/type_utilities.h>
#include <sensor_msgs/msg/joint_state.h>
#include <trajectory_msgs/msg/joint_trajectory.h>

static const char* TAG = "DogMicroros";

static const char* kJointNames[DEEP_DOG_MICROROS_JOINT_COUNT] = {
    "L1_joint", "L2_joint", "L3_joint", "L4_joint", "L5_joint", "L6_joint", "L7_joint",
};

static TaskHandle_t s_task = nullptr;
static SemaphoreHandle_t s_pos_mu = nullptr;
static double s_mock_pos[DEEP_DOG_MICROROS_JOINT_COUNT] = {};

static rcl_publisher_t s_js_pub;
static rcl_subscription_t s_traj_sub;
static rcl_timer_t s_js_timer;
static rclc_executor_t s_executor;
static rclc_support_t s_support;
static rcl_allocator_t s_allocator;
static rcl_node_t s_node;
static sensor_msgs__msg__JointState s_js_msg;
static trajectory_msgs__msg__JointTrajectory s_traj_msg;
static bool s_entities_ok = false;

#define RCCHECK(fn)                                                                                    \
    do {                                                                                               \
        rcl_ret_t temp_rc = (fn);                                                                      \
        if (temp_rc != RCL_RET_OK) {                                                                   \
            ESP_LOGE(TAG, "RCL fail %s:%d rc=%d", __FILE__, __LINE__, (int)temp_rc);                   \
            return false;                                                                              \
        }                                                                                              \
    } while (0)

#define RCSOFTCHECK(fn)                                                                                \
    do {                                                                                               \
        rcl_ret_t temp_rc = (fn);                                                                      \
        if (temp_rc != RCL_RET_OK) {                                                                   \
            ESP_LOGW(TAG, "RCL soft fail line %d rc=%d", __LINE__, (int)temp_rc);                       \
        }                                                                                              \
    } while (0)

static void CopyMockPositions(double* out, size_t n) {
    if (s_pos_mu && xSemaphoreTake(s_pos_mu, pdMS_TO_TICKS(20)) == pdTRUE) {
        for (size_t i = 0; i < n && i < DEEP_DOG_MICROROS_JOINT_COUNT; ++i) {
            out[i] = s_mock_pos[i];
        }
        xSemaphoreGive(s_pos_mu);
    } else {
        for (size_t i = 0; i < n; ++i) {
            out[i] = 0.0;
        }
    }
}

static void UpdateMockFromTrajectory(const trajectory_msgs__msg__JointTrajectory* msg) {
    if (msg == nullptr || msg->points.size == 0) {
        return;
    }
    const auto& pt = msg->points.data[0];
    if (s_pos_mu && xSemaphoreTake(s_pos_mu, pdMS_TO_TICKS(50)) == pdTRUE) {
        size_t n = pt.positions.size;
        if (n > DEEP_DOG_MICROROS_JOINT_COUNT) {
            n = DEEP_DOG_MICROROS_JOINT_COUNT;
        }
        for (size_t i = 0; i < n; ++i) {
            s_mock_pos[i] = pt.positions.data[i];
        }
        xSemaphoreGive(s_pos_mu);
    }
}

static void TrajectoryCallback(const void* msgin) {
    const auto* msg = static_cast<const trajectory_msgs__msg__JointTrajectory*>(msgin);
    if (msg == nullptr) {
        return;
    }
    ESP_LOGI(TAG, "traj points=%u joint_names=%u", (unsigned)msg->points.size,
             (unsigned)msg->joint_names.size);
    if (msg->points.size > 0 && msg->points.data[0].positions.size > 0) {
        ESP_LOGI(TAG, "traj[0] pos0=%.3f (n=%u)", msg->points.data[0].positions.data[0],
                 (unsigned)msg->points.data[0].positions.size);
    }
    UpdateMockFromTrajectory(msg);
}

static void JointStateTimerCallback(rcl_timer_t* timer, int64_t /*last_call_time*/) {
    if (timer == nullptr || !s_entities_ok) {
        return;
    }
    double pos[DEEP_DOG_MICROROS_JOINT_COUNT];
    CopyMockPositions(pos, DEEP_DOG_MICROROS_JOINT_COUNT);
    for (size_t i = 0; i < DEEP_DOG_MICROROS_JOINT_COUNT; ++i) {
        s_js_msg.position.data[i] = pos[i];
        s_js_msg.velocity.data[i] = 0.0;
        s_js_msg.effort.data[i] = 0.0;
    }
    s_js_msg.position.size = DEEP_DOG_MICROROS_JOINT_COUNT;
    s_js_msg.velocity.size = DEEP_DOG_MICROROS_JOINT_COUNT;
    s_js_msg.effort.size = DEEP_DOG_MICROROS_JOINT_COUNT;
    s_js_msg.name.size = DEEP_DOG_MICROROS_JOINT_COUNT;

    int64_t nanos = 0;
    (void)rmw_uros_epoch_nanos(&nanos);
    s_js_msg.header.stamp.sec = (int32_t)(nanos / 1000000000LL);
    s_js_msg.header.stamp.nanosec = (uint32_t)(nanos % 1000000000LL);

    RCSOFTCHECK(rcl_publish(&s_js_pub, &s_js_msg, nullptr));
}

static bool InitMessageMemory() {
    static bool once = false;
    if (once) {
        return true;
    }
    micro_ros_utilities_memory_conf_t conf = {};
    conf.max_string_capacity = DEEP_DOG_MICROROS_STRING_CAPACITY;
    conf.max_ros2_type_sequence_capacity = DEEP_DOG_MICROROS_JOINT_COUNT;
    conf.max_basic_type_sequence_capacity = DEEP_DOG_MICROROS_JOINT_COUNT;

    if (!micro_ros_utilities_create_message_memory(
            ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, JointState), &s_js_msg, conf)) {
        ESP_LOGE(TAG, "JointState memory alloc failed");
        return false;
    }
    for (size_t i = 0; i < DEEP_DOG_MICROROS_JOINT_COUNT; ++i) {
        s_js_msg.name.data[i] = micro_ros_string_utilities_set(s_js_msg.name.data[i], kJointNames[i]);
    }
    s_js_msg.name.size = DEEP_DOG_MICROROS_JOINT_COUNT;
    s_js_msg.position.size = DEEP_DOG_MICROROS_JOINT_COUNT;
    s_js_msg.velocity.size = DEEP_DOG_MICROROS_JOINT_COUNT;
    s_js_msg.effort.size = DEEP_DOG_MICROROS_JOINT_COUNT;

    micro_ros_utilities_memory_conf_t tconf = {};
    tconf.max_string_capacity = DEEP_DOG_MICROROS_STRING_CAPACITY;
    tconf.max_ros2_type_sequence_capacity = DEEP_DOG_MICROROS_TRAJ_MAX_POINTS;
    tconf.max_basic_type_sequence_capacity = DEEP_DOG_MICROROS_JOINT_COUNT;
    if (!micro_ros_utilities_create_message_memory(
            ROSIDL_GET_MSG_TYPE_SUPPORT(trajectory_msgs, msg, JointTrajectory), &s_traj_msg, tconf)) {
        ESP_LOGE(TAG, "JointTrajectory memory alloc failed");
        return false;
    }
    once = true;
    return true;
}

static void FiniEntities() {
    if (!s_entities_ok) {
        return;
    }
    s_entities_ok = false;
    (void)rclc_executor_fini(&s_executor);
    (void)rcl_timer_fini(&s_js_timer);
    (void)rcl_publisher_fini(&s_js_pub, &s_node);
    (void)rcl_subscription_fini(&s_traj_sub, &s_node);
    (void)rcl_node_fini(&s_node);
    (void)rclc_support_fini(&s_support);
    ESP_LOGW(TAG, "XRCE entities torn down");
}

static bool CreateEntities() {
    s_allocator = rcl_get_default_allocator();
    s_node = rcl_get_zero_initialized_node();
    s_js_pub = rcl_get_zero_initialized_publisher();
    s_traj_sub = rcl_get_zero_initialized_subscription();
    s_js_timer = rcl_get_zero_initialized_timer();
    s_executor = rclc_executor_get_zero_initialized_executor();

    rcl_init_options_t init_options = rcl_get_zero_initialized_init_options();
    RCCHECK(rcl_init_options_init(&init_options, s_allocator));

#ifdef CONFIG_MICRO_ROS_ESP_XRCE_DDS_MIDDLEWARE
    rmw_init_options_t* rmw_options = rcl_init_options_get_rmw_init_options(&init_options);
    RCCHECK(rmw_uros_options_set_udp_address(CONFIG_MICRO_ROS_AGENT_IP, CONFIG_MICRO_ROS_AGENT_PORT,
                                             rmw_options));
    ESP_LOGI(TAG, "Agent %s:%s", CONFIG_MICRO_ROS_AGENT_IP, CONFIG_MICRO_ROS_AGENT_PORT);
#else
    ESP_LOGE(TAG, "XRCE-DDS middleware not selected in menuconfig");
    return false;
#endif

    RCCHECK(rclc_support_init_with_options(&s_support, 0, nullptr, &init_options, &s_allocator));
    (void)rcl_init_options_fini(&init_options);

    RCCHECK(rclc_node_init_default(&s_node, DEEP_DOG_MICROROS_NODE_NAME, "", &s_support));

    RCCHECK(rclc_publisher_init_default(
        &s_js_pub, &s_node, ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, JointState),
        DEEP_DOG_MICROROS_JOINT_STATE_TOPIC));

    RCCHECK(rclc_subscription_init_default(
        &s_traj_sub, &s_node, ROSIDL_GET_MSG_TYPE_SUPPORT(trajectory_msgs, msg, JointTrajectory),
        DEEP_DOG_MICROROS_TRAJECTORY_TOPIC));

    RCCHECK(rclc_timer_init_default(&s_js_timer, &s_support,
                                    RCL_MS_TO_NS(DEEP_DOG_MICROROS_JOINT_STATE_PERIOD_MS),
                                    JointStateTimerCallback));

    RCCHECK(rclc_executor_init(&s_executor, &s_support.context, 2, &s_allocator));
    RCCHECK(rclc_executor_add_subscription(&s_executor, &s_traj_sub, &s_traj_msg, &TrajectoryCallback,
                                           ON_NEW_DATA));
    RCCHECK(rclc_executor_add_timer(&s_executor, &s_js_timer));

    s_entities_ok = true;
    ESP_LOGI(TAG, "session ready: pub %s @ %d Hz, sub %s", DEEP_DOG_MICROROS_JOINT_STATE_TOPIC,
             DEEP_DOG_MICROROS_JOINT_STATE_HZ, DEEP_DOG_MICROROS_TRAJECTORY_TOPIC);
    return true;
}

static void MicrorosTask(void* /*arg*/) {
    ESP_LOGI(TAG, "task start (reuse WifiBoard STA; no uros_network_interface_initialize)");

    if (s_pos_mu == nullptr) {
        s_pos_mu = xSemaphoreCreateMutex();
    }
    if (!InitMessageMemory()) {
        ESP_LOGE(TAG, "message memory init failed; task exit");
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    for (;;) {
        ESP_LOGI(TAG, "waiting for Agent ping...");
        while (rmw_uros_ping_agent(DEEP_DOG_MICROROS_PING_TIMEOUT_MS, DEEP_DOG_MICROROS_PING_ATTEMPTS) !=
               RMW_RET_OK) {
            vTaskDelay(pdMS_TO_TICKS(DEEP_DOG_MICROROS_RECONNECT_DELAY_MS));
        }
        ESP_LOGI(TAG, "Agent reachable, creating session");

        if (!CreateEntities()) {
            FiniEntities();
            vTaskDelay(pdMS_TO_TICKS(DEEP_DOG_MICROROS_RECONNECT_DELAY_MS));
            continue;
        }

        TickType_t last_ping = xTaskGetTickCount();
        while (s_entities_ok) {
            rclc_executor_spin_some(&s_executor, RCL_MS_TO_NS(50));
            if ((xTaskGetTickCount() - last_ping) >= pdMS_TO_TICKS(2000)) {
                last_ping = xTaskGetTickCount();
                if (rmw_uros_ping_agent(500, 1) != RMW_RET_OK) {
                    ESP_LOGW(TAG, "Agent ping lost; reconnect");
                    break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        FiniEntities();
        vTaskDelay(pdMS_TO_TICKS(DEEP_DOG_MICROROS_RECONNECT_DELAY_MS));
    }
}

void DeepDogMicrorosStart(void) {
    if (s_task != nullptr) {
        return;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(MicrorosTask, "uros_ce01", DEEP_DOG_MICROROS_TASK_STACK,
                                            nullptr, DEEP_DOG_MICROROS_TASK_PRIO, &s_task, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        s_task = nullptr;
    } else {
        ESP_LOGI(TAG, "uros task created");
    }
}

#endif  // DEEP_DOG_MICROROS_ENABLE
