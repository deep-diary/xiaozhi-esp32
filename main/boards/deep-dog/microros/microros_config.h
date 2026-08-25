#ifndef DEEP_DOG_MICROROS_CONFIG_H_
#define DEEP_DOG_MICROROS_CONFIG_H_

/**
 * CloudEdge CE01 — micro-ROS 话题与运行参数（契约不可擅自改名）。
 * 见 swrs/cloud_edge/CE01-microros-link-smoke.md · a3 TOPIC_CONTRACT.md
 */

#define DEEP_DOG_MICROROS_NODE_NAME "deep_dog_microros"

/** 轨迹输入（订阅） */
#define DEEP_DOG_MICROROS_TRAJECTORY_TOPIC "/joint_group_effort_controller/joint_trajectory"

/** 关节状态（发布） */
#define DEEP_DOG_MICROROS_JOINT_STATE_TOPIC "/joint_states"

#define DEEP_DOG_MICROROS_JOINT_COUNT 7

#define DEEP_DOG_MICROROS_JOINT_STATE_HZ 50
#define DEEP_DOG_MICROROS_JOINT_STATE_PERIOD_MS (1000 / DEEP_DOG_MICROROS_JOINT_STATE_HZ)

/** 轨迹消息容量（micro_ros_utilities） */
#define DEEP_DOG_MICROROS_TRAJ_MAX_POINTS 16
#define DEEP_DOG_MICROROS_STRING_CAPACITY 64

#define DEEP_DOG_MICROROS_TASK_STACK 16384
#define DEEP_DOG_MICROROS_TASK_PRIO 5

/** 入站轨迹业务处理上限（实验室 CHAMP loop_rate 联调 50 Hz；XRCE 仍 drain） */
#define DEEP_DOG_MICROROS_TRAJ_HANDLE_PERIOD_MS 20

/** Agent ping / 重连间隔 */
#define DEEP_DOG_MICROROS_PING_TIMEOUT_MS 1000
#define DEEP_DOG_MICROROS_PING_ATTEMPTS 3
#define DEEP_DOG_MICROROS_RECONNECT_DELAY_MS 2000

#endif  // DEEP_DOG_MICROROS_CONFIG_H_
