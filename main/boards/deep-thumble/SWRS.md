# 软件需求文档 (SWRS)

**项目名称**: Deep Thumble  
**开发目录**: `./main/boards/deep-thumble`  
**文档版本**: v1.5  
**最后更新**: 2025-01-XX

---

## 1. 2812 灯带需求

### 1.1 硬件配置
- **GPIO引脚**: GPIO_NUM_38
- **LED数量**: 24个
- **LED型号**: WS2812B（或兼容型号）
- **控制方式**: RMT驱动，10MHz分辨率
- **驱动实现**: 使用项目已有的 `CircularStrip` 类（详见 `main/led/circular_strip.cc`）

### 1.2 代码修改要求
- **删除UART相关代码**:
  - 删除 `config.h` 中的UART相关宏定义：
    - `UART_ECHO_TXD` (GPIO_NUM_38)
    - `UART_ECHO_RXD` (GPIO_NUM_48)
    - `ECHO_UART_PORT_NUM`
    - `ECHO_UART_BAUD_RATE`
    - `BUF_SIZE`（如果仅用于UART）
  - 删除 `deep_thumble.cc` 中的UART相关代码：
    - `InitializeEchoUart()` 方法
    - `SendUartMessage()` 方法
    - `#include <driver/uart.h>`（如果不再需要）
    - 构造函数中对 `InitializeEchoUart()` 的调用
  - 删除 `InitializeTools()` 中所有调用 `SendUartMessage()` 的MCP工具（如 `self.chassis.go_forward`、`self.chassis.go_back` 等）

### 1.3 功能实现要求
- **参考实现**: 参考 `main/boards/deep-diary/led/` 目录下的实现
- **核心类**: 使用 `CircularStrip` 类（位于 `main/led/circular_strip.h`）
- **控制类**: 创建 `LedStripControl` 类（参考 `main/boards/deep-thumble/led/led_control.cc`）
- **MCP工具注册**: 在 `LedStripControl` 构造函数中注册以下MCP工具：
  - `self.led_strip.get_brightness` - 获取亮度等级（0-8）
  - `self.led_strip.set_brightness` - 设置亮度等级（0-8）
  - `self.led_strip.set_single_color` - 设置单个LED颜色
  - `self.led_strip.set_all_color` - 设置所有LED颜色
  - `self.led_strip.blink` - 闪烁效果
  - `self.led_strip.scroll` - 跑马灯效果
  - `self.led_strip.breathe` - 呼吸灯效果
  - `self.led_strip.show_device_state` - 显示设备状态

### 1.4 初始化流程
1. 在 `DeepThumble` 类中添加 `CircularStrip* led_strip_` 成员变量
2. 在 `DeepThumble` 类中添加 `LedStripControl* led_control_` 成员变量
3. 添加 `InitializeLedStrip()` 方法，创建 `CircularStrip` 对象
4. 在构造函数中调用 `InitializeLedStrip()` 和创建 `LedStripControl` 对象
5. 在析构函数中释放相关资源

### 1.5 配置要求
- 在 `config.h` 中添加：
  ```c
  #define WS2812_STRIP_GPIO GPIO_NUM_38
  #define WS2812_LED_COUNT 24
  ```

---

## 2. 人脸识别需求

### 2.1 参考代码
- **参考项目**: `./ref/human_face_recognition_no_wakenet/`
- **参考文档**: `./ref/human_face_recognition_no_wakenet/项目架构分析.md`
- **核心组件**: ESP-DL库（人脸检测/识别模型）

### 2.2 移植要求
- **代码位置**: 所有AI相关代码放在 `./ai/` 目录下
- **架构适配**: 
  - 优先使用本项目现有接口（`Esp32Camera`、音频接口等）
  - 参考代码仅作为逻辑参考，整体架构需融入当前项目
  - 可复用的模块可直接复制（如ESP-DL组件）
- **任务创建**: 在 `deep_thumble.cc` 中创建独立的AI任务（FreeRTOS任务）

### 2.3 功能要求

#### 2.3.1 核心功能
- **人脸检测**: 实时检测画面中的人脸
- **人脸识别**: 识别已注册的人脸
- **人脸注册**: 自动注册新人脸（当未识别到匹配人脸时）
- **移动侦测**: 检测画面中的物体移动（灵敏度可配置）

#### 2.3.2 运行逻辑
- **循环执行**: 人脸检测、注册/识别、移动侦测循环进行
- **识别间隔**: 可宏定义，默认 `FACE_RECOGNITION_INTERVAL_MS = 500`（0.5秒）
- **人脸库管理**:
  - 本地默认保存5张人脸（可宏定义 `MAX_FACE_COUNT = 5`）
  - 如果未识别到匹配的人脸，则将最新的人脸替换原有注册过的人脸
  - 每次注册或识别时，更新已注册人脸的最后识别时间
  - 替换策略：替换最长时间未识别到的人脸，确保注册的人脸库永远是最新见到的几个人
- **人脸命名逻辑**:
  - 服务器返回 `unknown` 时，将人脸命名为 `unknown`
  - 新人脸先请求服务器识别：
    - 如果服务器返回确定姓名，则将对应人脸设置为该姓名
    - 如果服务器返回 `unknown` 或无法识别，则命名为 `unknown2`
    - 如果 `unknown2` 已存在，则命名为 `unknown3`，依次类推
- **MCP命令**: 暂不需要MCP命令控制（覆盖相当于删除，注册都自动注册）

#### 2.3.3 远程识别

##### 2.3.3.1 触发条件
- **默认触发条件**: 无法识别到对应人物的姓名（姓名通过远程返回）
- **触发时机**: 人脸识别失败后，直接调用远程识别接口
- **可配置**: 通过宏定义 `ENABLE_REMOTE_RECOGNITION` 控制是否启用

##### 2.3.3.2 调用方式
- **接口方法**: 直接调用 `Esp32Camera::Explain()` 方法
- **请求参数**: 
  - 问题描述：`"人脸识别"`（便于服务器识别需求）
  - 图像数据：当前摄像头捕获的图像（自动包含）
- **服务器处理**: 服务器收到explain接口发送的图片后进行识别并返回结果
- **超时时间**: 默认3秒（可宏定义 `REMOTE_RECOGNITION_TIMEOUT_MS = 3000`）

##### 2.3.3.3 调用状态管理
- **问题**: 识别失败时会调用远程接口，但调用接口期间可能会不断重复调用
- **解决方案**: 需要添加调用状态位（`bool is_remote_recognition_in_progress_`）
- **实现要求**:
  - 调用远程接口前检查状态位，如果正在调用则跳过
  - 开始调用时设置状态位为 `true`
  - 接口响应结束后（成功、失败或超时）设置状态位为 `false`
  - 确保同一时间只有一个远程识别请求在进行

##### 2.3.3.4 响应处理流程

**重要说明**: `Esp32Camera::Explain()` 方法仅发送HTTP POST请求，服务器返回的是AI响应的文字及语音。需要在响应的JSON文本中提取人脸识别结果字段。

**当前架构**:
1. `Explain()` 方法返回JSON字符串（HTTP响应体）
2. 返回格式示例：`{"success": true, "result": "分析结果文本"}`
3. 服务器可能通过WebSocket的LLM消息进一步返回AI响应（文字和语音）

**响应处理位置**:
- **方案一**: 在 `Explain()` 返回的JSON中解析（推荐）
  - 位置：人脸识别任务中调用 `Explain()` 后立即解析返回的JSON
  - 优点：同步处理，逻辑清晰
  - 缺点：需要服务器在HTTP响应中包含人脸识别结果
- **方案二**: 在WebSocket的LLM消息中解析
  - 位置：`main/application.cc` 的 `OnIncomingJson` 回调中，处理 `type: "llm"` 消息
  - 优点：可以利用现有的WebSocket消息流
  - 缺点：异步处理，需要关联请求和响应

**推荐方案**: 采用方案一，在 `Explain()` 返回的JSON中直接解析人脸识别结果。

##### 2.3.3.5 协议设计

**HTTP响应JSON格式**（`Explain()` 返回）:
```json
{
  "success": true,
  "result": "AI分析的文本结果",
  "face_recognition": {
    "detected": true,
    "face_id": 1,
    "name": "张三",
    "confidence": 0.95
  }
}
```

**字段说明**:
- `success`: 布尔值，表示请求是否成功
- `result`: 字符串，AI分析的文本结果（用于TTS播放）
- `face_recognition`: 对象，人脸识别结果（可选字段）
  - `detected`: 布尔值，是否检测到人脸
  - `face_id`: 整数，识别到的人脸ID（本地注册的人脸ID）
  - `name`: 字符串，识别到的人脸姓名（服务器返回的姓名）
  - `confidence`: 浮点数，识别置信度（0.0-1.0）

**WebSocket LLM消息格式**（如果服务器通过WebSocket返回）:
```json
{
  "session_id": "xxx",
  "type": "llm",
  "emotion": "happy",
  "text": "AI响应的文本",
  "face_recognition": {
    "detected": true,
    "face_id": 1,
    "name": "张三",
    "confidence": 0.95
  }
}
```

**字段说明**:
- `type`: 固定为 `"llm"`
- `emotion`: 字符串，表情动画（现有字段）
- `text`: 字符串，AI响应的文本（现有字段）
- `face_recognition`: 对象，人脸识别结果（新增字段，可选）

##### 2.3.3.6 结果提取和处理

**在HTTP响应中提取**（推荐）:
```cpp
// 伪代码示例
std::string response_json = camera->Explain("人脸识别");
cJSON* root = cJSON_Parse(response_json.c_str());

if (root) {
    // 检查是否有face_recognition字段
    cJSON* face_recognition = cJSON_GetObjectItem(root, "face_recognition");
    if (face_recognition && cJSON_IsObject(face_recognition)) {
        cJSON* detected = cJSON_GetObjectItem(face_recognition, "detected");
        cJSON* face_id = cJSON_GetObjectItem(face_recognition, "face_id");
        cJSON* name = cJSON_GetObjectItem(face_recognition, "name");
        
        if (cJSON_IsTrue(detected) && 
            cJSON_IsNumber(face_id) && 
            cJSON_IsString(name)) {
            int id = face_id->valueint;
            std::string face_name = name->valuestring;
            
            // 调用设置姓名方法
            face_recognition_->SetFaceName(id, face_name);
        }
    }
    cJSON_Delete(root);
}
```

**在WebSocket LLM消息中提取**（备选方案）:
- 在 `main/application.cc` 的 `OnIncomingJson` 回调中，处理 `type: "llm"` 消息时
- 检查是否有 `face_recognition` 字段
- 提取 `face_id` 和 `name`，调用 `FaceRecognition::SetFaceName()`

##### 2.3.3.7 结果处理
- **服务器返回**: JSON格式响应，包含 `face_recognition.people[]` 数组，每个元素包含 `name` 和 `person_id`
- **姓名匹配**: 
  - 根据返回的 `name` 调用 `FindFaceIdByName()` 匹配本地人脸ID
  - 如果匹配到，调用 `SetFaceName(face_id, name)` 设置姓名
  - 如果未匹配到（新人脸），调用 `GetUnrecognizedFaceId()` 获取新人脸ID，然后设置姓名
- **多人处理**: 遍历 `people` 数组，为每个人物匹配或设置姓名
- **本地识别优先**: 如果本地可以正常识别到人脸，则无需调用远程识别

#### 2.3.4 液晶显示功能与显示策略

##### 2.3.4.0 显示策略（人脸/运动识别）
- **后台运行**: 人脸检测、人脸识别、运动侦测逻辑**仅在后台运行**，平时**不在屏幕上显示**任何识别结果或预览画面，以节省内存和避免打扰。
- **仅注册时显示**:
  - **新注册时显示一次**：当发生**新人脸注册**（写入本地人脸库）时，在屏幕上显示一次（例如提示「已注册：xxx」或带人脸框的预览），显示后即恢复不显示。
  - **已注册人物再次识别到**：离线识别到**已注册**的人物时，**不再在屏幕显示**；仅更新内部状态（如 `last_seen_ms`）供主动唤醒等逻辑使用。
- **再次显示条件**：仅当再次发生**新注册**（新人物写入人脸库）时，再在屏幕上显示一次。
- **与 Explain 的区分**：调用 `Explain()` 进行远程识别时，若需在屏上显示图片/结果，按上述策略（仅在新注册时显示）；其余时机不因识别结果刷新屏幕。

##### 2.3.4.1 图像显示
- **显示接口**: `Esp32Camera::Explain()` 方法本身具备在液晶屏幕上显示图片的功能
- **显示时机**: 按 2.3.4.0 显示策略，**仅在需要展示注册结果等少数时机**使用；平时不因人脸识别而刷新屏幕
- **显示格式**: RGB565格式，适配LCD显示

##### 2.3.4.2 人脸框和标注
- **参考代码**: 参考 `ref/human_face_recognition_no_wakenet/` 目录下的实现
- **绘制函数**: 
  - `draw_detection_result()` - 绘制检测框和关键点（位于 `components/modules/ai/who_ai_utils.hpp`）
  - `rgb_print()` / `rgb_printf()` - 绘制文字标注（位于 `main/src/app_face.cpp`）
- **图形库**: 使用 `fb_gfx` 库进行图形绘制
- **实现要求**:
  - 在显示图像上绘制人脸检测框
  - 在检测框附近显示人脸ID和姓名（如果已识别）
  - 未识别时显示 "who ?" 提示
  - 标注样式：使用默认样式或推荐样式（颜色、字体大小等可参考参考代码）
  - 参考代码中的绘制逻辑可直接复用或适配
- **显示时机**: 人脸框与标注**仅在新注册时显示一次**（见 2.3.4.0），平时不绘制到屏幕

#### 2.3.5 主动唤醒功能

##### 2.3.5.1 唤醒触发条件
- **时间阈值**: 判断上次见面时间，如果超过10分钟（可宏定义 `FACE_RECOGNITION_WAKE_THRESHOLD_MS = 600000`），则主动唤醒
- **首次识别**: 如果设备刚开机，第一次识别到人物时，也主动唤醒
- **初始时间设置**: 上次识别到的时间初始值设置为很大的负数（如 `INT64_MIN` 或 `-LLONG_MAX`），确保第一次识别时间间隔就可以超过阈值

##### 2.3.5.2 唤醒实现
- **唤醒方法**: 调用 `Application::GetInstance().StartListening()` 方法
- **唤醒时机**: 在500ms周期的用户主循环任务中判断并触发
- **唤醒逻辑**:
  ```cpp
  // 伪代码示例
  int64_t current_time = esp_timer_get_time() / 1000; // 毫秒
  int64_t time_since_last_recognition = current_time - last_recognition_time_;
  
  if (time_since_last_recognition > FACE_RECOGNITION_WAKE_THRESHOLD_MS) {
      Application::GetInstance().StartListening();
      // 更新最后识别时间
      last_recognition_time_ = current_time;
  }
  ```

### 2.4 类设计要求

#### 2.4.1 类名
建议使用 `FaceRecognition` 或 `FaceRecognitionManager`

#### 2.4.2 状态（成员变量）
```cpp
// 当前是否有人脸
bool has_face_;

// 是否有识别到的人脸
bool has_recognized_face_;
int recognized_face_id_;              // 识别到的人脸ID
std::string recognized_face_name_;    // 识别到的人脸姓名

// 当前是否有移动
bool has_motion_;

// 远程识别调用状态
bool is_remote_recognition_in_progress_;  // 是否正在调用远程识别接口

// 人脸识别时间记录
int64_t last_recognition_time_;           // 上次识别到人脸的时间（毫秒时间戳）
std::map<int, int64_t> face_last_seen_time_;  // 每个人脸ID的最后识别时间
```

#### 2.4.3 方法（公共接口）
```cpp
// 设置某个ID的人脸姓名
void SetFaceName(int face_id, const std::string& name);

// 获取当前状态
bool HasFace() const;
bool HasRecognizedFace() const;
int GetRecognizedFaceId() const;
std::string GetRecognizedFaceName() const;
bool HasMotion() const;

// 远程识别状态管理
bool IsRemoteRecognitionInProgress() const;
void SetRemoteRecognitionInProgress(bool in_progress);

// 时间管理
int64_t GetLastRecognitionTime() const;
void UpdateLastRecognitionTime(int face_id);

// 移动侦测灵敏度配置
void SetMotionSensitivity(float sensitivity);
float GetMotionSensitivity() const;

// 主动唤醒检查
void CheckAndWakeIfNeeded();

// 姓名匹配方法（用于服务器返回结果匹配）
int FindFaceIdByName(const std::string& name) const;
int GetUnrecognizedFaceId() const;
std::map<int, std::string> GetAllFaceNames() const;

// 启动/停止识别任务
void Start();
void Stop();
```

#### 2.4.4 回调函数（函数指针或std::function）
```cpp
// 检测到人脸回调
std::function<void()> on_face_detected_callback_;

// 识别到人脸回调（参数：face_id, face_name）
std::function<void(int, const std::string&)> on_face_recognized_callback_;

// 未识别到人脸回调
std::function<void()> on_face_unrecognized_callback_;

// 移动回调
std::function<void()> on_motion_detected_callback_;
```

#### 2.4.5 内部实现
- **人脸检测**: 使用ESP-DL的 `HumanFaceDetectMSR01` 和 `HumanFaceDetectMNP01` 模型
- **人脸识别**: 使用ESP-DL的 `FaceRecognition112V1S8` 或 `FaceRecognition112V1S16` 模型
- **移动侦测**: 参考 `ref/` 目录下的 `app_motion.cpp` 实现，灵敏度通过变量配置
- **数据存储**: 使用assets分区（SPIFFS文件系统）存储人脸特征数据
  - 存储位置：`/assets/face_recognition/`
  - 存储文件：每个人脸的特征数据保存为独立文件（如 `face_0.dat`, `face_1.dat`）
  - 元数据文件：`faces_metadata.json` 存储人脸ID、姓名、最后识别时间等信息

### 2.5 文件结构
```
./ai/
├── face_recognition.h          # 人脸识别类头文件
├── face_recognition.cc         # 人脸识别类实现
├── face_recognition_task.cc    # AI任务实现（FreeRTOS任务）
└── README.md                   # AI模块说明文档
```

### 2.6 配置宏定义
在 `config.h` 或新建 `ai_config.h` 中添加：
```c
// 人脸识别配置
#define FACE_RECOGNITION_INTERVAL_MS          500     // 识别间隔（毫秒）
#define MAX_FACE_COUNT                        5       // 最大人脸数量
#define FACE_RECOGNITION_THRESHOLD            0.98    // 识别阈值（相似度）
#define ENABLE_REMOTE_RECOGNITION             1       // 是否启用远程识别（1=启用，0=禁用）
#define REMOTE_RECOGNITION_TIMEOUT_MS         3000    // 远程识别超时时间（毫秒）
#define FACE_RECOGNITION_WAKE_THRESHOLD_MS    600000  // 主动唤醒时间阈值（10分钟，毫秒）
#define FACE_STORAGE_PATH                     "/assets/face_recognition/"  // 人脸数据存储路径
```

### 2.7 集成到主程序
- 在 `DeepThumble` 类中添加 `FaceRecognition* face_recognition_` 成员变量
- 添加 `InitializeFaceRecognition()` 方法
- 在构造函数中调用 `InitializeFaceRecognition()`
- 创建AI任务（参考 `deep-diary` 的 `user_main_loop_task` 实现）

---

## 3. 传感器采集需求

### 3.1 六轴加速度传感器
- **传感器类型**: BMI270（六轴加速度传感器：加速度计+陀螺仪）
- **参考实现**: 
  - 优先参考本仓库中的 BMI270 集成：`main/boards/esp-spot/esp_spot_board.cc` 中的 `Bmi270Imu` 命名空间（初始化、I2C 总线复用、低功耗与唤醒）
  - 也可参考外部示例：`ref/factory_demo_v1/main/app/app_imu.c` 和 `app_datafusion.c`
- **代码位置**: 在 `./sensor/` 目录下实现
- **采集周期**: 10ms（在用户主循环的10ms周期任务中调用）

### 3.2 功能要求
- **数据采集**: 采集加速度计和陀螺仪的原始数据
- **数据处理**: 可选的数据融合处理（如姿态角计算）
- **数据存储**: 采集的数据可用于后续业务逻辑（如姿态检测、运动检测等）

### 3.3 文件结构
```
./sensor/
├── imu_sensor.h          # 传感器类头文件
├── imu_sensor.cc         # 传感器类实现
└── README.md             # 传感器模块说明文档
```

### 3.4 参考代码说明
- **参考文件**: `ref/factory_demo_v1/main/app/app_imu.c`
  - 使用BMI270传感器
  - 10ms周期采集数据
  - 包含数据融合和姿态计算
- **数据融合**: `ref/factory_demo_v1/main/app/app_datafusion.c`
  - 使用卡尔曼滤波进行数据融合
  - 计算俯仰角（pitch）、翻滚角（roll）、偏航角（yaw）

### 3.5 集成要求
- 在用户主循环的10ms周期任务中调用传感器数据采集
- 传感器初始化在 `DeepThumble` 构造函数中完成

---

## 4. 应用逻辑需求

### 4.1 架构设计
- **主文件**: `deep_thumble.cc` 作为入口，但应保持代码简洁
- **用户主循环**: 创建独立的用户主循环任务，避免在 `deep_thumble.cc` 中堆积过多业务逻辑
- **文件组织**: 用户逻辑放在单独文件中实现，便于维护和扩展

### 4.2 用户主循环任务

#### 4.2.1 任务创建
- **文件位置**: 创建 `user_main_loop.cc` 和 `user_main_loop.h`（单独文件）
- **任务名称**: `user_main_loop_task`
- **任务优先级**: 建议优先级4（参考 `deep-diary`）
- **栈大小**: 建议8192字节（可根据实际需求调整）

#### 4.2.2 调度机制
- **基础周期**: 10ms循环一次
- **计数器机制**: 使用计数器实现多周期调度
- **调度周期**: 支持以下周期（单位：毫秒）
  - **10ms**  - 高频任务（传感器数据采集）
  - 50ms  - 中频任务
  - 100ms - 中频任务
  - **500ms** - 低频任务（人脸识别唤醒判断）
  - 1000ms (1秒) - 秒级任务
  - 5000ms (5秒) - 长时间任务
  - 10000ms (10秒) - 超长时间任务

#### 4.2.3 实现方式
参考 `main/boards/deep-diary/deep_diary.cc` 中的 `user_main_loop_task` 实现：
```cpp
// 基础延时宏定义
#define MAIN_LOOP_BASE_DELAY_MS    10

// 周期倍数定义（实际周期 = 倍数 × MAIN_LOOP_BASE_DELAY_MS）
#define CYCLE_10MS       1      // 10ms
#define CYCLE_50MS       5      // 50ms
#define CYCLE_100MS      10     // 100ms
#define CYCLE_500MS      50     // 500ms
#define CYCLE_1S         100    // 1秒
#define CYCLE_5S         500    // 5秒
#define CYCLE_10S        1000   // 10秒

// 使用计数器判断是否到达指定周期
if ((cycle_counter % CYCLE_10MS) == 0) {
    // 执行10ms周期的任务（传感器数据采集）
    imu_sensor_->ReadData();
}

if ((cycle_counter % CYCLE_500MS) == 0) {
    // 执行500ms周期的任务（人脸识别唤醒判断）
    face_recognition_->CheckAndWakeIfNeeded();
}
```

#### 4.2.4 任务函数组织
- **文件分离**: 每个调度周期对应的函数可以考虑放在单独文件中
- **扩展性**: 后续新增业务逻辑时，根据调度周期在对应文件中添加调用即可
- **初始实现**: 初始状态只需打印log，便于实际调试验证代码功能

#### 4.2.5 具体任务分配

##### 10ms周期任务
- **传感器数据采集**: 调用 `ImuSensor::ReadData()` 获取六轴加速度传感器数据
- **数据预处理**: 可选的数据预处理（如滤波、单位转换等）

##### 500ms周期任务
- **人脸识别唤醒判断**: 
  - 检查上次识别到人脸的时间
  - 如果超过10分钟阈值，调用 `Application::GetInstance().StartListening()` 主动唤醒
  - 如果是第一次识别（初始时间为很大的负数），也主动唤醒
  - 更新最后识别时间

#### 4.2.6 示例结构
```
user_main_loop.cc          # 主循环任务实现
├── 10ms周期任务函数      # 传感器数据采集
├── 50ms周期任务函数      # 中频任务（预留）
├── 100ms周期任务函数     # 中频任务（预留）
├── 500ms周期任务函数     # 人脸识别唤醒判断
├── 1s周期任务函数        # 秒级任务（预留）
├── 5s周期任务函数        # 长时间任务（预留）
└── 10s周期任务函数       # 超长时间任务（预留）

// 或者按功能模块组织：
scheduler/
├── scheduler_10ms.cc     # 10ms周期任务
├── scheduler_50ms.cc    # 50ms周期任务
├── scheduler_100ms.cc    # 100ms周期任务
└── ...
```

### 4.3 集成要求
- **初始化**: 在 `DeepThumble` 构造函数中创建用户主循环任务
- **任务句柄**: 保存任务句柄，便于后续管理
- **资源管理**: 在析构函数中删除任务

### 4.4 注意事项
- **架构适配**: 上述架构仅为参考，最终需基于当前主架构进行扩展和适配
- **代码量控制**: 避免 `deep_thumble.cc` 代码量过多，保持其作为入口文件的简洁性
- **可扩展性**: 设计时应考虑后续功能扩展的便利性

---

## 5. 技术架构说明

### 5.1 项目架构
- **基础框架**: 基于ESP-IDF和FreeRTOS
- **通信协议**: MCP（Model Context Protocol）用于语音控制
- **硬件抽象**: `WifiBoard` 基类提供硬件抽象接口
- **任务管理**: 使用FreeRTOS任务进行多任务并发处理

### 5.2 关键组件
- **摄像头**: `Esp32Camera` 类（位于 `main/boards/common/esp32_camera.h`）
- **显示**: `Display` 接口（LCD显示）
- **音频**: `Es8311AudioCodec` 类
- **LED控制**: `CircularStrip` 类（位于 `main/led/circular_strip.h`）
- **MCP服务器**: `McpServer` 单例（位于 `main/mcp_server.h`）
- **应用管理**: `Application` 单例（位于 `main/application.h`），提供 `StartListening()` 方法用于主动唤醒

### 5.3 参考实现
- **LED控制**: 参考 `main/boards/deep-diary/led/` 目录
- **用户主循环**: 参考 `main/boards/deep-diary/deep_diary.cc` 的 `user_main_loop_task`
- **任务创建**: 参考 `deep-diary` 中的任务创建方式
- **传感器采集**: 参考 `ref/factory_demo_v1/main/app/app_imu.c`

---

## 6. 开发注意事项

### 6.1 代码规范
- 使用C++17标准
- 遵循项目现有的代码风格
- 添加必要的注释和文档

### 6.2 错误处理
- 所有硬件初始化应使用 `ESP_ERROR_CHECK()` 进行错误检查
- 任务创建失败应记录日志
- 资源分配失败应有适当的错误处理
- 远程识别接口调用失败应有重试机制或错误处理
- 传感器数据采集失败应有容错处理
- JSON解析失败应有错误处理

### 6.3 内存管理
- 注意PSRAM的使用（摄像头图像缓冲）
- 避免内存泄漏，确保资源正确释放
- 注意栈溢出问题，合理设置任务栈大小
- 人脸识别数据存储需考虑内存占用
- 传感器数据缓冲区大小需合理设置
- JSON解析后及时释放cJSON对象

### 6.4 性能考虑
- AI任务可能占用较多CPU资源，注意任务优先级设置
- 人脸识别间隔可配置，避免过于频繁导致性能问题
- LED刷新频率需平衡视觉效果和性能
- 远程识别接口调用可能耗时较长（网络延迟），需异步处理避免阻塞
- 传感器数据采集频率需平衡精度和性能
- JSON解析性能需考虑（使用cJSON库）

### 6.5 调试支持
- 使用 `ESP_LOGI`、`ESP_LOGW`、`ESP_LOGE` 进行日志输出
- 关键状态变化应记录日志（如人脸检测、识别、远程调用、主动唤醒等）
- 初始实现应包含足够的日志输出便于调试
- 远程识别接口调用应记录请求和响应日志
- 传感器数据可选择性输出日志便于调试
- JSON解析结果应记录日志便于调试

---

## 7. 待确认事项

1. **LED数量**: ✅ 已确认 - 24个，驱动使用项目已有的 `CircularStrip` 类
2. **人脸存储**: ✅ 已确认 - 使用assets分区（SPIFFS文件系统）存储人脸数据
3. **远程识别**: ✅ 已确认 - 直接调用 `Explain()` 接口，服务器自动识别并返回，超时时间3秒
4. **调度周期**: ✅ 已确认 - 10ms（传感器采集）、500ms（人脸唤醒判断），其他周期预留扩展
5. **传感器类型**: ✅ 已确认 - BMI270
6. **协议字段**: ✅ 已确认 - HTTP响应返回，格式已定义（见第8章）

---

## 8. 服务器协议对接说明

### 8.1 当前协议流程

1. **设备端调用**: `Esp32Camera::Explain("人脸识别")`
   - 发送HTTP POST请求到服务器
   - 请求体：multipart/form-data，包含question和image文件
   - 请求头：`Device-Id`、`Client-Id`、`Authorization: Bearer <token>`

2. **服务器响应**: HTTP响应返回JSON字符串
   - **当前格式**：`{"success": true, "action": "RESPONSE", "response": "AI分析文本"}`
   - **扩展格式**：在现有格式基础上添加 `face_recognition` 字段

3. **设备端处理**: 
   - 解析HTTP响应的JSON
   - 提取 `face_recognition` 字段
   - 根据返回的姓名匹配本地人脸ID
   - 调用 `SetFaceName(face_id, name)` 设置姓名

### 8.2 服务器端现状分析

根据服务器代码（[vision_handler.py](https://github.com/deep-diary/xiaozhi-esp32-server/blob/9f959980e81dee2fb3ee2dfac870434680f8a90c/main/xiaozhi-server/core/api/vision_handler.py)）：

1. **当前返回格式**:
   ```python
   return_json = {
       "success": True,
       "action": Action.RESPONSE.name,  # 值为 "RESPONSE"
       "response": result,  # AI分析的文本结果
   }
   ```

2. **已有功能**:
   - 使用Immich API上传图片并识别人物
   - `people_info` 包含识别结果：
     - `people`: 人物列表，每个包含 `person_name` 和 `person_id`（Immich的ID）
     - `asset_id`: Immich资产ID
   - 目前将人物信息整合到question中，让VLLM在回答中包含人物信息

3. **需要扩展**:
   - 在返回JSON中添加 `face_recognition` 字段
   - 包含Immich识别到的人物信息

### 8.3 协议字段定义（最终确认）

#### 8.3.1 HTTP响应JSON格式

**扩展后的完整格式**:
```json
{
  "success": true,
  "action": "RESPONSE",
  "response": "这是AI分析的文本结果，用于TTS播放和显示",
  "face_recognition": {
    "detected": true,
    "people": [
      {
        "person_id": "immich_person_id_1",
        "name": "张三",
        "confidence": 0.95
      },
      {
        "person_id": "immich_person_id_2",
        "name": "李四",
        "confidence": 0.88
      }
    ]
  }
}
```

**字段说明**:
- `success`: 布尔值，必需，表示请求是否成功
- `action`: 字符串，必需，固定为 `"RESPONSE"`（保持现有协议）
- `response`: 字符串，必需，AI分析的文本结果（用于TTS播放和显示）
- `face_recognition`: 对象，可选，人脸识别结果
  - `detected`: 布尔值，是否检测到人脸
  - `people`: 数组，识别到的人物列表（可能识别到多个人）
    - `person_id`: 字符串，Immich的人物ID（可选，用于服务器端关联）
    - `name`: 字符串，识别到的人物姓名（必需）
      - 如果Immich识别到确定姓名，返回该姓名（如"张三"）
      - 如果Immich识别到但未命名，返回 `"未命名"` 或 `null`
      - 如果无法识别，返回 `"unknown"`
    - `confidence`: 浮点数，识别置信度（0.0-1.0，可选）

**特殊情况处理**:
- **未检测到人脸**: `face_recognition` 字段可以不存在，或 `detected: false`, `people: []`
- **检测到人脸但无法识别**: `detected: true`, `people: [{"name": "unknown"}]`
- **识别到多个人**: `people` 数组包含多个元素
- **识别到但未命名**: `people: [{"name": "未命名"}]` 或 `{"name": null}`

#### 8.3.2 设备端匹配逻辑

**关键点**: 服务器返回的是Immich的 `person_id` 和 `person_name`，设备端需要根据 `name` 匹配本地人脸ID。

**匹配策略**:
1. **优先匹配**: 根据返回的 `name` 匹配本地已有人脸的姓名
   - 如果本地人脸库中已有相同姓名的记录，使用该 `face_id`
   - 如果匹配到多个同名，使用最近识别到的那个人脸ID

2. **新人脸处理**: 如果无法匹配到本地人脸（新人脸或未命名）
   - 如果 `name` 为 `"unknown"` 或 `null` 或 `"未命名"`：
     - 使用新人脸命名逻辑（`unknown`、`unknown2`、`unknown3` 等）
   - 如果 `name` 为确定姓名：
     - 找到当前未识别的人脸（刚注册的新人脸）
     - 设置该人脸的姓名为返回的 `name`

3. **多人识别**: 如果 `people` 数组包含多个人
   - 遍历数组，为每个人物匹配或设置姓名
   - 优先处理置信度最高的人物

#### 8.3.3 设备端实现示例

```cpp
// 伪代码示例（在 face_recognition_task.cc 中）
std::string response_json = camera->Explain("人脸识别");
cJSON* root = cJSON_Parse(response_json.c_str());

if (root) {
    // 检查是否有face_recognition字段
    cJSON* face_recognition = cJSON_GetObjectItem(root, "face_recognition");
    if (face_recognition && cJSON_IsObject(face_recognition)) {
        cJSON* detected = cJSON_GetObjectItem(face_recognition, "detected");
        cJSON* people = cJSON_GetObjectItem(face_recognition, "people");
        
        if (cJSON_IsTrue(detected) && cJSON_IsArray(people)) {
            int people_count = cJSON_GetArraySize(people);
            
            // 遍历识别到的人物列表
            for (int i = 0; i < people_count; i++) {
                cJSON* person = cJSON_GetArrayItem(people, i);
                if (!cJSON_IsObject(person)) continue;
                
                cJSON* name_item = cJSON_GetObjectItem(person, "name");
                cJSON* person_id_item = cJSON_GetObjectItem(person, "person_id");
                cJSON* confidence_item = cJSON_GetObjectItem(person, "confidence");
                
                if (cJSON_IsString(name_item)) {
                    std::string person_name = name_item->valuestring;
                    
                    // 跳过未命名和unknown的情况（已在本地处理）
                    if (person_name == "未命名" || person_name == "unknown" || person_name.empty()) {
                        continue;
                    }
                    
                    // 根据姓名匹配本地人脸ID
                    int local_face_id = face_recognition_->FindFaceIdByName(person_name);
                    
                    if (local_face_id >= 0) {
                        // 匹配到本地人脸，更新姓名（可能服务器返回了更准确的姓名）
                        face_recognition_->SetFaceName(local_face_id, person_name);
                        ESP_LOGI(TAG, "Matched face: ID=%d, Name=%s", local_face_id, person_name.c_str());
                    } else {
                        // 未匹配到，可能是新人脸，找到当前未识别的人脸
                        int new_face_id = face_recognition_->GetUnrecognizedFaceId();
                        if (new_face_id >= 0) {
                            face_recognition_->SetFaceName(new_face_id, person_name);
                            ESP_LOGI(TAG, "Set new face name: ID=%d, Name=%s", new_face_id, person_name.c_str());
                        }
                    }
                }
            }
        }
    }
    cJSON_Delete(root);
}
```

#### 8.3.4 服务器端实现建议

**修改位置**: `vision_handler.py` 的 `handle_post` 方法

**修改内容**:
1. 在构建 `return_json` 时，添加 `face_recognition` 字段
2. 从 `people_info` 中提取人物信息，构建 `people` 数组
3. 保持现有字段不变（`success`、`action`、`response`）

**服务器端代码修改示例**:
```python
# 在 vision_handler.py 的 handle_post 方法中
# 构建返回结果，保持原有协议不变
return_json = {
    "success": True,
    "action": Action.RESPONSE.name,
    "response": result,
}

# 新增：添加人脸识别结果
if people_info and people_info.get("success") and people_info.get("people"):
    people_list = people_info.get("people", [])
    face_recognition_data = {
        "detected": True,
        "people": []
    }
    
    for person in people_list:
        person_name = person.get("person_name", "unknown")
        person_id = person.get("person_id")
        
        # 只添加有效的人物信息（排除空值和"未命名"）
        if person_name and person_name != "未命名":
            person_data = {
                "name": person_name
            }
            if person_id:
                person_data["person_id"] = person_id
            # 可选：添加置信度
            # person_data["confidence"] = person.get("confidence", 0.0)
            
            face_recognition_data["people"].append(person_data)
    
    # 如果识别到人物，添加到返回JSON
    if face_recognition_data["people"]:
        return_json["face_recognition"] = face_recognition_data
    else:
        # 如果识别到但都是未命名，标记为detected但people为空
        return_json["face_recognition"] = {
            "detected": True,
            "people": []
        }
else:
    # 未检测到人脸或识别失败
    return_json["face_recognition"] = {
        "detected": False,
        "people": []
    }
```

### 8.4 设备端类设计补充

需要在 `FaceRecognition` 类中添加以下方法支持姓名匹配：

```cpp
// 根据姓名查找本地人脸ID
// 返回匹配到的face_id，如果未找到返回-1
int FindFaceIdByName(const std::string& name) const;

// 获取当前未识别的人脸ID（新人脸）
// 返回未设置姓名的人脸ID，如果都已有姓名则返回-1
int GetUnrecognizedFaceId() const;

// 获取所有人脸的姓名映射（用于调试）
std::map<int, std::string> GetAllFaceNames() const;
```

**实现建议**:
- `FindFaceIdByName()`: 遍历本地人脸库，查找姓名匹配的人脸
- `GetUnrecognizedFaceId()`: 查找姓名为空或为"unknown"系列的人脸
- 如果匹配到多个同名，返回最近识别到的那个人脸ID

### 8.5 服务器端代码修改说明

**修改文件**: `vision_handler.py` 的 `handle_post` 方法

**修改位置**: 在构建 `return_json` 之后，返回响应之前

**修改代码**:
```python
# 在现有代码中，构建return_json后添加以下代码：

# 添加人脸识别结果
if people_info and people_info.get("success") and people_info.get("people"):
    people_list = people_info.get("people", [])
    face_recognition_data = {
        "detected": True,
        "people": []
    }
    
    for person in people_list:
        person_name = person.get("person_name", "unknown")
        person_id = person.get("person_id")
        
        # 只添加有效的人物信息
        # 注意：这里保留所有人物，包括"未命名"，设备端会处理
        person_data = {
            "name": person_name if person_name else "unknown"
        }
        if person_id:
            person_data["person_id"] = person_id
        
        face_recognition_data["people"].append(person_data)
    
    # 如果识别到人物，添加到返回JSON
    if face_recognition_data["people"]:
        return_json["face_recognition"] = face_recognition_data
    else:
        # 如果people为空，标记为detected但people为空
        return_json["face_recognition"] = {
            "detected": True,
            "people": []
        }
else:
    # 未检测到人脸或识别失败
    return_json["face_recognition"] = {
        "detected": False,
        "people": []
    }
```

**注意事项**:
1. 保持现有字段不变（`success`、`action`、`response`）
2. `face_recognition` 字段为可选，如果Immich未启用或识别失败，也应包含该字段（`detected: false`）
3. `people` 数组可能为空（未识别到人物）或包含多个元素（识别到多个人）
4. `person_id` 字段可选，主要用于服务器端关联，设备端主要使用 `name` 字段

### 8.6 协议对接检查清单

- [x] 确认服务器在HTTP响应中添加 `face_recognition` 字段
- [x] 确认字段格式：`face_recognition.people[]` 数组，包含 `name` 和 `person_id`
- [x] 确认 `name` 字段的格式和可能的值（确定姓名、"unknown"、"未命名"等）
- [x] 确认设备端根据 `name` 匹配本地人脸ID的逻辑
- [x] 确认多人识别的处理方式（遍历 `people` 数组）
- [x] 确认服务器端代码修改位置和方式
- [ ] 服务器端代码修改完成并测试
- [ ] 设备端代码实现并测试
- [ ] 端到端联调测试

### 8.7 协议设计总结

#### 8.7.1 关键设计要点

1. **保持向后兼容**: 
   - 保持现有字段不变（`success`、`action`、`response`）
   - `face_recognition` 为可选字段，不影响现有功能

2. **支持多人识别**: 
   - 使用 `people` 数组支持识别多个人物
   - 设备端遍历数组处理每个人物

3. **姓名匹配策略**: 
   - 服务器返回Immich的 `person_name`
   - 设备端根据 `name` 匹配本地人脸ID
   - 避免服务器端需要知道设备端本地ID的复杂性

4. **错误处理**: 
   - 未检测到人脸：`detected: false`
   - 检测到但无法识别：`detected: true`, `people: [{"name": "unknown"}]`
   - 识别到但未命名：`people: [{"name": "未命名"}]`

#### 8.7.2 数据流

```
设备端检测到未识别人脸
    ↓
调用 Explain("人脸识别")
    ↓
服务器接收图片，调用Immich API识别
    ↓
服务器返回JSON（包含face_recognition字段）
    ↓
设备端解析JSON，提取people数组
    ↓
遍历people数组，根据name匹配本地人脸ID
    ↓
调用SetFaceName(face_id, name)设置姓名
```

#### 8.7.3 服务器端修改要点

参考服务器代码：[vision_handler.py](https://github.com/deep-diary/xiaozhi-esp32-server/blob/9f959980e81dee2fb3ee2dfac870434680f8a90c/main/xiaozhi-server/core/api/vision_handler.py)

**修改位置**: `handle_post` 方法中，构建 `return_json` 之后

**关键代码**:
- 从 `people_info` 中提取 `people` 列表
- 构建 `face_recognition` 对象
- 添加到 `return_json` 中

**注意事项**:
- 保持现有字段和逻辑不变
- `face_recognition` 字段始终存在（即使未检测到人脸）
- `people` 数组可能为空

---

## 9. 开发计划建议

### 阶段一：2812灯带功能
1. 删除UART相关代码
2. 实现LED灯带初始化（使用 `CircularStrip` 类）
3. 实现MCP控制接口
4. 测试验证

### 阶段二：传感器采集功能
1. 创建传感器模块（`sensor/` 目录）
2. 实现BMI270传感器初始化
3. 集成到用户主循环10ms周期任务
4. 测试验证

### 阶段三：用户主循环框架
1. 创建用户主循环任务框架
2. 实现调度机制
3. 实现10ms和500ms周期任务
4. 添加初始日志输出
5. 测试验证

### 阶段四：人脸识别功能
1. 移植ESP-DL组件
2. 实现人脸识别类（包含调用状态位管理、时间管理）
3. 实现人脸数据存储（assets分区）
4. 集成到主程序
5. 实现远程识别接口（含状态管理、超时处理）
6. 实现HTTP响应JSON解析（提取人脸识别结果）
7. 实现液晶显示功能（图像显示、人脸框和标注）
8. 实现主动唤醒功能（500ms周期判断）
9. 实现人脸命名逻辑（unknown系列）
10. 与服务器对接确认协议格式
11. 测试验证

---

**文档维护**: 本文档应根据开发进展及时更新，记录实际实现与需求的差异。
