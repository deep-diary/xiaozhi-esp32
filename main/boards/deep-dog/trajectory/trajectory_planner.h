#ifndef TRAJECTORY_PLANNER_H
#define TRAJECTORY_PLANNER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 轨迹规划配置
#define MAX_TRAJECTORY_POINTS 1000    // 最大轨迹点数
#define TRAJECTORY_MAX_MOTOR_COUNT 6  // 最大电机数量
#define INTERPOLATION_FACTOR 4        // 插值倍数，用于三次样条插值
#define POINT_TO_POINT_FACTOR 180     // 点对点轨迹插值倍数
#define POINT_TO_POINT_DURATION_MS 3000  // 点对点轨迹执行时长(ms)
#define TRAJECTORY_SAMPLE_INTERVAL_MS 50  // 轨迹规划采样间隔(ms)
#define MIN_TRAJECTORY_POINTS 50      // 最小轨迹点数，确保平滑性
#define MAX_ORIGINAL_POINTS 200        // 最大原始点数，用于三次样条插值（减少栈使用）
#define SPLINE_BATCH_SIZE 10          // 样条插值批处理大小

// 轨迹点结构体
typedef struct {
    float positions[TRAJECTORY_MAX_MOTOR_COUNT];  // 6个电机的位置
    float velocities[TRAJECTORY_MAX_MOTOR_COUNT]; // 6个电机的速度
    uint32_t time_ms;                 // 时间戳(ms)
} trajectory_point_t;

// 轨迹规划参数
typedef struct {
    float max_velocity[TRAJECTORY_MAX_MOTOR_COUNT];  // 最大速度 (rad/s)
    float max_acceleration[TRAJECTORY_MAX_MOTOR_COUNT]; // 最大加速度 (rad/s²)
    float max_jerk[TRAJECTORY_MAX_MOTOR_COUNT];      // 最大加加速度 (rad/s³)
    uint32_t interpolation_factor;        // 插值倍数
} trajectory_config_t;

// 轨迹规划器结构体
typedef struct {
    trajectory_point_t points[MAX_TRAJECTORY_POINTS];
    uint16_t point_count;
    uint16_t current_index;
    bool is_active;
    trajectory_config_t config;
} trajectory_planner_t;

/**
 * @brief 初始化轨迹规划器
 * @param planner 轨迹规划器指针
 */
void trajectory_planner_init(trajectory_planner_t* planner);

/**
 * @brief 规划两点间的轨迹
 * @param planner 轨迹规划器指针
 * @param start_point 起始点
 * @param end_point 结束点
 * @param config 配置参数
 * @return true 成功, false 失败
 */
bool trajectory_plan_point_to_point(trajectory_planner_t* planner, 
                                   const trajectory_point_t* start_point,
                                   const trajectory_point_t* end_point,
                                   const trajectory_config_t* config);

/**
 * @brief 对现有轨迹进行插值
 * @param planner 轨迹规划器指针
 * @param original_points 原始轨迹点
 * @param original_count 原始点数
 * @param target_points 目标点数
 * @return true 成功, false 失败
 */
bool trajectory_interpolate(trajectory_planner_t* planner,
                           const trajectory_point_t* original_points,
                           uint16_t original_count,
                           uint16_t target_points);

/**
 * @brief 获取下一个轨迹点
 * @param planner 轨迹规划器指针
 * @param point 输出轨迹点
 * @return true 有下一个点, false 轨迹结束
 */
bool trajectory_get_next_point(trajectory_planner_t* planner, trajectory_point_t* point);

/**
 * @brief 重置轨迹规划器
 * @param planner 轨迹规划器指针
 */
void trajectory_reset(trajectory_planner_t* planner);

/**
 * @brief 检查轨迹是否完成
 * @param planner 轨迹规划器指针
 * @return true 完成, false 未完成
 */
bool trajectory_is_finished(const trajectory_planner_t* planner);

/**
 * @brief 获取轨迹总点数
 * @param planner 轨迹规划器指针
 * @return 轨迹点数
 */
uint16_t trajectory_get_point_count(const trajectory_planner_t* planner);

/**
 * @brief 使用三次样条插值对轨迹进行平滑处理
 * @param planner 轨迹规划器指针
 * @param original_points 原始轨迹点数组
 * @param original_count 原始轨迹点数量
 * @param target_points 目标插值点数
 * @return true 插值成功, false 插值失败
 */
bool trajectory_cubic_spline_interpolate(trajectory_planner_t* planner, 
                                         const trajectory_point_t original_points[], 
                                         uint16_t original_count, 
                                         uint16_t target_points);

/**
 * @brief 流式三次样条插值（避免栈溢出）
 * @param planner 轨迹规划器指针
 * @param original_points 原始轨迹点数组
 * @param original_count 原始轨迹点数量
 * @param target_points 目标插值点数
 * @param callback 每生成一个点时的回调函数
 * @return true 插值成功, false 插值失败
 */
bool trajectory_cubic_spline_stream(trajectory_planner_t* planner,
                                   const trajectory_point_t original_points[],
                                   uint16_t original_count,
                                   uint16_t target_points,
                                   void (*callback)(const trajectory_point_t* point, uint16_t index));

/**
 * @brief 打印轨迹点信息（用于验证）
 * @param planner 轨迹规划器指针
 * @param max_points 最大打印点数（0表示打印所有点）
 */
void trajectory_print_points(trajectory_planner_t* planner, uint16_t max_points);

#ifdef __cplusplus
}
#endif

#endif // TRAJECTORY_PLANNER_H
