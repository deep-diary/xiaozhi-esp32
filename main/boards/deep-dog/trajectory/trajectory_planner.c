#include "trajectory_planner.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char* TAG = "TrajectoryPlanner";

// 初始化轨迹规划器
void trajectory_planner_init(trajectory_planner_t* planner) {
    if (!planner) {
        ESP_LOGE(TAG, "轨迹规划器指针无效");
        return;
    }
    
    memset(planner, 0, sizeof(trajectory_planner_t));
    ESP_LOGI(TAG, "轨迹规划器初始化完成");
}

// 计算S曲线速度
static float calculate_s_curve_velocity(float t, float total_time, float max_velocity, float max_acceleration) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 0.0f;
    
    // 简化的S曲线：加速 -> 匀速 -> 减速
    float accel_time = 0.3f;  // 30%时间加速
    float decel_time = 0.3f;  // 30%时间减速
    float const_time = 1.0f - accel_time - decel_time;  // 40%时间匀速
    
    if (const_time < 0.0f) {
        // 如果时间太短，使用三角形速度曲线
        accel_time = 0.5f;
        decel_time = 0.5f;
        const_time = 0.0f;
    }
    
    if (t <= accel_time) {
        // 加速阶段
        float accel_t = t / accel_time;
        return max_velocity * accel_t;
    } else if (t <= accel_time + const_time) {
        // 匀速阶段
        return max_velocity;
    } else {
        // 减速阶段
        float decel_t = (t - accel_time - const_time) / decel_time;
        return max_velocity * (1.0f - decel_t);
    }
}

// 计算轨迹段
static void calculate_trajectory_segment(const trajectory_point_t* start, 
                                       const trajectory_point_t* end,
                                       trajectory_point_t* output_points,
                                       uint16_t point_count,
                                       const trajectory_config_t* config) {
    for (uint16_t i = 0; i < point_count; i++) {
        float t = (float)i / (float)(point_count - 1);
        
        for (int j = 0; j < TRAJECTORY_MAX_MOTOR_COUNT; j++) {
            float distance = end->positions[j] - start->positions[j];
            float velocity = calculate_s_curve_velocity(t, 1.0f, 
                                                      config->max_velocity[j],
                                                      config->max_acceleration[j]);
            
            output_points[i].positions[j] = start->positions[j] + t * distance;
            
            // 计算速度，考虑距离和方向
            float velocity_magnitude = velocity * fabsf(distance);
            output_points[i].velocities[j] = velocity_magnitude * (distance >= 0 ? 1.0f : -1.0f);
            
            // 确保最小速度，避免完全静止
            if (fabsf(output_points[i].velocities[j]) < 0.01f && fabsf(distance) > 0.001f) {
                output_points[i].velocities[j] = (distance >= 0 ? 0.01f : -0.01f);
            }
        }
        
        output_points[i].time_ms = start->time_ms + (uint32_t)(t * (end->time_ms - start->time_ms));
    }
}

// 点对点轨迹规划
bool trajectory_plan_point_to_point(trajectory_planner_t* planner, 
                                   const trajectory_point_t* start_point,
                                   const trajectory_point_t* end_point,
                                   const trajectory_config_t* config) {
    if (!planner || !start_point || !end_point || !config) {
        ESP_LOGE(TAG, "点对点轨迹规划参数无效");
        return false;
    }
    
    // 计算最大运动时间
    float max_time = 0.0f;
    for (int i = 0; i < TRAJECTORY_MAX_MOTOR_COUNT; i++) {
        float distance = fabsf(end_point->positions[i] - start_point->positions[i]);
        float t_total = 0.0f;
        
        if (distance > 0.0f && config->max_velocity[i] > 0.0f) {
            // 简化的时间计算：考虑加速、匀速、减速
            float accel_time = config->max_velocity[i] / config->max_acceleration[i];
            float const_distance = distance - config->max_velocity[i] * accel_time;
            float const_time = (const_distance > 0.0f) ? const_distance / config->max_velocity[i] : 0.0f;
            t_total = 2.0f * accel_time + const_time;
            
            if (t_total > max_time) max_time = t_total;
        }
    }
    
    // 生成轨迹点 - 使用点对点插值倍数
    uint16_t point_count = POINT_TO_POINT_FACTOR; // 点对点轨迹插值倍数 = 100个点
    
    // 确保最小轨迹点数，保证平滑性
    if (point_count < MIN_TRAJECTORY_POINTS) {
        point_count = MIN_TRAJECTORY_POINTS;
        ESP_LOGW(TAG, "轨迹点数不足，调整为最小点数: %d", MIN_TRAJECTORY_POINTS);
    }
    
    if (point_count > MAX_TRAJECTORY_POINTS) {
        point_count = MAX_TRAJECTORY_POINTS;
        ESP_LOGW(TAG, "轨迹点数过多，限制为最大点数: %d", MAX_TRAJECTORY_POINTS);
    }
    
    calculate_trajectory_segment(start_point, end_point, planner->points, point_count, config);
    
    planner->point_count = point_count;
    planner->current_index = 0;
    
    ESP_LOGI(TAG, "点对点轨迹规划完成：%d 个点，预计时间: %.2fs", point_count, max_time);
    return true;
}

// 轨迹插值
bool trajectory_interpolate(trajectory_planner_t* planner, 
                           const trajectory_point_t* original_points,
                           uint16_t original_count,
                           uint16_t target_points) {
    if (!planner || !original_points || original_count < 2 || target_points < original_count) {
        ESP_LOGE(TAG, "轨迹插值参数无效");
        return false;
    }
    
    // 确保最小轨迹点数，保证平滑性
    if (target_points < MIN_TRAJECTORY_POINTS) {
        target_points = MIN_TRAJECTORY_POINTS;
        ESP_LOGW(TAG, "目标点数不足，调整为最小点数: %d", MIN_TRAJECTORY_POINTS);
    }
    
    if (target_points > MAX_TRAJECTORY_POINTS) {
        ESP_LOGW(TAG, "目标点数 (%d) 超过最大限制 (%d)", target_points, MAX_TRAJECTORY_POINTS);
        target_points = MAX_TRAJECTORY_POINTS;
    }
    
    planner->point_count = 0;
    planner->current_index = 0;
    
    for (uint16_t i = 0; i < target_points; i++) {
        float t = (float)i / (float)(target_points - 1);
        uint16_t segment = (uint16_t)(t * (original_count - 1));
        float local_t = t * (original_count - 1) - segment;
        
        if (segment >= original_count - 1) {
            segment = original_count - 2;
            local_t = 1.0f;
        }
        
        trajectory_point_t* point = &planner->points[planner->point_count];
        point->time_ms = original_points[0].time_ms + 
            (uint32_t)(t * (original_points[original_count - 1].time_ms - original_points[0].time_ms));
        
        for (int j = 0; j < TRAJECTORY_MAX_MOTOR_COUNT; j++) {
            point->positions[j] = original_points[segment].positions[j] + 
                local_t * (original_points[segment + 1].positions[j] - original_points[segment].positions[j]);
            point->velocities[j] = original_points[segment].velocities[j] + 
                local_t * (original_points[segment + 1].velocities[j] - original_points[segment].velocities[j]);
        }
        
        planner->point_count++;
    }
    
    ESP_LOGI(TAG, "轨迹插值完成：%d 个点", planner->point_count);
    return true;
}

// 流式三次样条插值实现（避免栈溢出）
bool trajectory_cubic_spline_stream(trajectory_planner_t* planner,
                                   const trajectory_point_t original_points[],
                                   uint16_t original_count,
                                   uint16_t target_points,
                                   void (*callback)(const trajectory_point_t* point, uint16_t index)) {
    if (!planner || !original_points || original_count < 3 || target_points < original_count) {
        ESP_LOGE(TAG, "流式样条插值参数无效");
        return false;
    }
    
    if (target_points > MAX_TRAJECTORY_POINTS) {
        ESP_LOGW(TAG, "目标点数 (%d) 超过最大限制 (%d)", target_points, MAX_TRAJECTORY_POINTS);
        target_points = MAX_TRAJECTORY_POINTS;
    }
    
    // 限制原始点数，确保内存使用可控
    if (original_count > MAX_ORIGINAL_POINTS) {
        ESP_LOGE(TAG, "原始点数 (%d) 超过最大限制 (%d)", original_count, MAX_ORIGINAL_POINTS);
        return false;
    }
    
    ESP_LOGI(TAG, "开始流式样条插值：原始 %d 点 -> 目标 %d 点", original_count, target_points);
    
    // 使用批处理方式，每次只处理少量数据，避免栈溢出
    uint16_t processed_points = 0;
    
    while (processed_points < target_points) {
        uint16_t batch_size = (target_points - processed_points > SPLINE_BATCH_SIZE) ? 
                              SPLINE_BATCH_SIZE : (target_points - processed_points);
        
        // 计算当前批次的插值点
        for (uint16_t i = 0; i < batch_size; i++) {
            uint16_t global_index = processed_points + i;
            float t = (float)global_index / (float)(target_points - 1);
            
            // 找到对应的原始点区间
            uint16_t segment = 0;
            float local_t = 0.0f;
            
            if (global_index == 0) {
                segment = 0;
                local_t = 0.0f;
            } else if (global_index == target_points - 1) {
                segment = original_count - 2;
                local_t = 1.0f;
            } else {
                // 线性插值找到对应的段
                float segment_t = t * (original_count - 1);
                segment = (uint16_t)segment_t;
                local_t = segment_t - segment;
                
                if (segment >= original_count - 1) {
                    segment = original_count - 2;
                    local_t = 1.0f;
                }
            }
            
            // 生成插值点
            trajectory_point_t interpolated_point;
            interpolated_point.time_ms = original_points[0].time_ms + 
                (uint32_t)(t * (original_points[original_count - 1].time_ms - original_points[0].time_ms));
            
            // 对每个电机进行线性插值（简化版本，避免复杂的三次样条计算）
            for (int motor = 0; motor < TRAJECTORY_MAX_MOTOR_COUNT; motor++) {
                float start_pos = original_points[segment].positions[motor];
                float end_pos = original_points[segment + 1].positions[motor];
                float start_vel = original_points[segment].velocities[motor];
                float end_vel = original_points[segment + 1].velocities[motor];
                
                // 使用三次多项式插值（简化版）
                float pos = start_pos + local_t * (end_pos - start_pos);
                float vel = start_vel + local_t * (end_vel - start_vel);
                
                interpolated_point.positions[motor] = pos;
                interpolated_point.velocities[motor] = vel;
            }
            
            // 调用回调函数处理当前点
            if (callback) {
                callback(&interpolated_point, global_index);
            }
        }
        
        processed_points += batch_size;
        
        // 让出CPU时间，避免阻塞
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    ESP_LOGI(TAG, "流式样条插值完成：共处理 %d 个点", processed_points);
    return true;
}

// 三次样条插值实现（简化版本，避免栈溢出）
bool trajectory_cubic_spline_interpolate(trajectory_planner_t* planner, 
                                         const trajectory_point_t original_points[], 
                                         uint16_t original_count, 
                                         uint16_t target_points) {
    if (!planner || !original_points || original_count < 3 || target_points < original_count) {
        ESP_LOGE(TAG, "三次样条插值参数无效");
        return false;
    }
    
    // 确保最小轨迹点数，保证平滑性
    if (target_points < MIN_TRAJECTORY_POINTS) {
        target_points = MIN_TRAJECTORY_POINTS;
        ESP_LOGW(TAG, "目标点数不足，调整为最小点数: %d", MIN_TRAJECTORY_POINTS);
    }
    
    if (target_points > MAX_TRAJECTORY_POINTS) {
        ESP_LOGW(TAG, "目标点数 (%d) 超过最大限制 (%d)", target_points, MAX_TRAJECTORY_POINTS);
        target_points = MAX_TRAJECTORY_POINTS;
    }
    
    // 限制原始点数，确保内存使用可控
    if (original_count > MAX_ORIGINAL_POINTS) {
        ESP_LOGE(TAG, "原始点数 (%d) 超过最大限制 (%d)", original_count, MAX_ORIGINAL_POINTS);
        return false;
    }
    
    ESP_LOGI(TAG, "开始简化样条插值：原始 %d 点 -> 目标 %d 点", original_count, target_points);
    
    // 初始化轨迹规划器
    planner->point_count = 0;
    
    // 使用简化的线性插值（避免复杂的三次样条计算）
    for (uint16_t i = 0; i < target_points; i++) {
        float t = (float)i / (float)(target_points - 1);
        
        // 找到对应的原始点区间
        uint16_t segment = 0;
        float local_t = 0.0f;
        
        if (i == 0) {
            segment = 0;
            local_t = 0.0f;
        } else if (i == target_points - 1) {
            segment = original_count - 2;
            local_t = 1.0f;
        } else {
            // 线性插值找到对应的段
            float segment_t = t * (original_count - 1);
            segment = (uint16_t)segment_t;
            local_t = segment_t - segment;
            
            if (segment >= original_count - 1) {
                segment = original_count - 2;
                local_t = 1.0f;
            }
        }
        
        // 生成插值点
        trajectory_point_t* point = &planner->points[planner->point_count];
        point->time_ms = original_points[0].time_ms + 
            (uint32_t)(t * (original_points[original_count - 1].time_ms - original_points[0].time_ms));
        
        // 对每个电机进行线性插值
        for (int motor = 0; motor < TRAJECTORY_MAX_MOTOR_COUNT; motor++) {
            float start_pos = original_points[segment].positions[motor];
            float end_pos = original_points[segment + 1].positions[motor];
            float start_vel = original_points[segment].velocities[motor];
            float end_vel = original_points[segment + 1].velocities[motor];
            
            // 线性插值
            point->positions[motor] = start_pos + local_t * (end_pos - start_pos);
            point->velocities[motor] = start_vel + local_t * (end_vel - start_vel);
        }
        
        planner->point_count++;
        
        // 每处理10个点让出CPU时间
        if (i % 10 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    
    ESP_LOGI(TAG, "简化样条插值完成：共生成 %d 个点", planner->point_count);
    return true;
}

// 获取下一个轨迹点
bool trajectory_get_next_point(trajectory_planner_t* planner, trajectory_point_t* point) {
    if (!planner || !point) {
        return false;
    }
    
    if (planner->current_index >= planner->point_count) {
        return false;
    }
    
    *point = planner->points[planner->current_index];
    planner->current_index++;
    return true;
}

// 重置轨迹规划器
void trajectory_reset(trajectory_planner_t* planner) {
    if (planner) {
        planner->current_index = 0;
    }
}

// 检查轨迹是否完成
bool trajectory_is_finished(const trajectory_planner_t* planner) {
    return planner && planner->current_index >= planner->point_count;
}

// 获取轨迹点数
uint16_t trajectory_get_point_count(const trajectory_planner_t* planner) {
    return planner ? planner->point_count : 0;
}

// 打印轨迹点信息（用于验证）
void trajectory_print_points(trajectory_planner_t* planner, uint16_t max_points) {
    if (!planner) {
        ESP_LOGE(TAG, "轨迹规划器指针无效");
        return;
    }
    
    uint16_t print_count = (max_points == 0 || max_points > planner->point_count) ? 
                          planner->point_count : max_points;
    
    ESP_LOGI(TAG, "=== 轨迹点信息 ===");
    ESP_LOGI(TAG, "总点数: %d, 打印点数: %d", planner->point_count, print_count);
    
    for (uint16_t i = 0; i < print_count; i++) {
        const trajectory_point_t* point = &planner->points[i];
        ESP_LOGI(TAG, "点 %3d: 时间=%5lums, 位置=[%6.3f, %6.3f, %6.3f, %6.3f, %6.3f, %6.3f], 速度=[%6.3f, %6.3f, %6.3f, %6.3f, %6.3f, %6.3f]",
                 i, point->time_ms,
                 point->positions[0], point->positions[1], point->positions[2], 
                 point->positions[3], point->positions[4], point->positions[5],
                 point->velocities[0], point->velocities[1], point->velocities[2], 
                 point->velocities[3], point->velocities[4], point->velocities[5]);
    }
    
    ESP_LOGI(TAG, "=== 轨迹点信息结束 ===");
}