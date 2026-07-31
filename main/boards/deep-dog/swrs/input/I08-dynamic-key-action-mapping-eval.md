# I08 · 动态按键→动作映射：可行性评估

| 项 | 内容 |
|----|------|
| 域 | input / handle · led（可推广） |
| 类型 | **评估**（非交付规格）；结论供 ROADMAP 选型 |
| 动机 | 编译期「一手柄一 App、键位写死」→ 希望 Web/MQTT 改绑定并 NVS 固化 |
| 现状对照 | [I01](./I01-architecture.md) · [I04](./I04-apps-mapping.md) · [08-led](../mqtt/modules/08-led.md) |
| 状态 | 评估完成；契约已拍板见 [I08a](./I08a-keymap-mqtt-contract-draft.md)；**固件 planned** |

---

## 1. 问题陈述

当前手柄链路：

```text
源(Normalize) → HandleSnapshot → Hub → Dispatcher → HandleApp*（编译期写死 if a→… / if b→…）
```

[I04](./I04-apps-mapping.md) 明确：**无运行时 NVS 绑定**（与 touch v0.1 一致）。固件烧好后，例如「A→站立、B→停」无法改，只能改代码重编。

设想：把灯带（及以后狗/舵机）可控动作列成表，按键只保存「指向哪个动作」；Web 发 MQTT 改表项（如对换 A/B），写入 NVS，实现**不重刷固件改映射**。

核心问题：

1. **能否做？**（技术可行性）
2. **有没有必要做？**（产品/工程价值）
3. **嵌入式里常见吗？**（行业惯例）
4. **若做，怎么落才稳？**（推荐形态，避免踩坑）

---

## 2. 结论摘要

| 问题 | 结论 |
|------|------|
| 可行性 | **可行**。本质是「输入事件 → 动作 ID → 执行器」的绑定表；MQTT 改表 + NVS 持久化在 ESP-IDF 上成熟。 |
| 原设想「结构体里塞函数指针，MQTT 改指针」 | **方向对，落点需改**：NVS/MQTT **不能存函数指针**（地址随链接/OTA 变）；应存 **action_id（枚举/整数）+ 可选参数**，运行时查表得到函数。 |
| 必要性（deep-dog 当前） | **灯带演示 / 用户可定制场景有价值**；**运控主路径（狗行走）短期不必做**。v0.1 继续编译期 App；动态映射作 **v0.x 可选增强**。 |
| 业界使用频率 | **很常见**：游戏手柄 remap、遥控器学习码、智能家居场景、工业 HMI 键位配置；少见的是「裸函数指针写 Flash」。 |

一句话：**想法正确；实现上用「动作 ID 表」而不是「可持久化的函数指针」；灯带层优先试点，勿先动狗运控。**

---

## 3. 可行性分析

### 3.1 原设想拆解

| 步骤 | 原说法 | 评估 |
|------|--------|------|
| 列出灯带控制函数 | `ApplyStatic` / Blink / Breathe… | ✅ 已有 [`LedStripControl`](../../led/led_strip_control.h)；MQTT `led/cmd` 已能动态改灯效 |
| C 结构体 + 函数指针 | `map[KEY_A] = &red` | ✅ 运行时派发可用；⚠️ **不要**把指针写入 NVS/MQTT |
| Web → MQTT 改映射 | 对换 A/B | ✅ 需新增 Topic（或扩 `handle/cmd`），载荷用 **action_id** |
| 写入 NVS 固化 | 断电仍有效 | ✅ 与 touch 阈值、MQTT broker、人脸库同一套路 |

### 3.2 为何不能直接存函数指针

```text
❌ NVS / MQTT JSON 存 void (*)(void)
   · Flash 镜像不同版本 → 函数地址变化
   · OTA 后旧指针 → 野指针崩溃
   · 跨编译器/优化级别不可移植

✅ 存稳定标识
   · action_id: uint8/uint16 或短字符串 "led.static_red"
   · 可选 params: { r,g,b, mode, interval_ms, … }
   · 开机：id → 固件内静态表 → 真正的函数入口
```

这是命令模式（Command）/ 键位表（Keymap）的标准做法，不是否定「函数指针」——指针只留在 **RAM 里的派发表**，由 ID 填充。

### 3.3 推荐数据模型（示意）

```c
typedef enum {
    ACT_NONE = 0,
    ACT_LED_OFF,
    ACT_LED_STATIC,      // params: r,g,b,brightness
    ACT_LED_BLINK,
    ACT_LED_BREATHE,
    ACT_LED_SCROLL,
    // 远期：ACT_DOG_STAND, ACT_DOG_STOP, …
    ACT_COUNT
} ActionId;

typedef struct {
    ActionId id;
    uint8_t  r, g, b;
    uint8_t  brightness;
    uint16_t interval_ms;
    // …按需扩展；NVS 用定长 blob 或 cJSON
} ActionBinding;

// 离散键：边沿 press 触发一次
ActionBinding key_map[KEY_COUNT];  // KEY_A, KEY_B, … 与 HandleSnapshot.buttons 对齐
```

派发：

```text
buttons.a 上升沿 → lookup(key_map[KEY_A]) → actions[id](params) → LedStripControl::Apply*
```

连续量（摇杆）一般 **不**进「按一下换一个函数」表，仍由运控 App 读 axes；动态映射优先覆盖 **面键 / 肩键 / D-pad** 等离散输入。

### 3.4 与现有架构如何嵌

**推荐落点：新增一个 Handle App（如 `HandleAppRemap` / `HandleAppLedMap`），而不是拆掉 Hub。**

```text
HandleEventHub
    └─ Dispatcher fan-out
         ├─ HandleAppLog          （不变）
         ├─ HandleAppDog          （编译期运控；可与 remap 互斥或门控）
         └─ HandleAppLedMap ★     （读 NVS 表；离散键 → LedStripControl）
              ↑
         MQTT handle/keymap 或 led/keymap
              ↑
         Web UI；成功后写 NVS + 可选 retain status
```

| 原则 | 说明 |
|------|------|
| 保持 I01 分层 | 源仍只出 Snapshot；**映射只在 App 内**（与 I04 原则一致，只是 App 从表读而非写死） |
| 不替代 `led/cmd` | 网页直接改灯效继续走 [08-led](../mqtt/modules/08-led.md)；keymap 只解决「按键触发哪条灯效」 |
| 与 dog App 冲突 | 同一键勿双执行：`disable` dog、或 remap 占用键时 dog 忽略该键、或分 profile |
| touch 对照 | touch 也是编译期 App；阈值才 NVS。动态映射若做，**先手柄离散键**，touch 可后跟 |

### 3.5 MQTT / NVS 草图（评估级，非契约定稿）

**下行（改映射）** 示例：

```json
{
  "action": "set_keymap",
  "bindings": {
    "a": { "id": "led.static", "r": 255, "g": 0, "b": 0 },
    "b": { "id": "led.static", "r": 0, "g": 0, "b": 255 }
  },
  "persist": true,
  "ts": 1710000000
}
```

对换 A/B：交换两个 `bindings` 项即可，无需改固件。

**上行（回读）**：retain `handle/keymap` 或并入 status 扩展字段，供 Web 打开即显示当前表。

**NVS**：namespace 如 `h_keymap`；存 JSON 或定长数组；校验 `schema_ver` + `action_id < ACT_COUNT`，非法则回落出厂默认。

### 3.6 风险与边界

| 风险 | 缓解 |
|------|------|
| 动作爆炸、表难维护 | 灯带先收敛到现有 mode 0–4 + 颜色参数；禁止无限自定义脚本 |
| 参数与 ID 不一致 | 绑定结构固定字段；未知 id → `ACT_NONE` + 日志 |
| 运控安全 | 行走/站立等 **默认不开放** 用户 remap；或仅 debug profile |
| 多 App 抢同一键 | 文档约定优先级；或 keymap App 独占「演示 profile」 |
| Flash/RAM | 表很小（几十～几百字节）；相对 BLE 可忽略 |
| 前端复杂度 | 需映射编辑 UI；契约变更要同步 deep-trace（见板规） |

**技术可行性：高。** 主要成本在契约、UI、与现有 App 的互斥策略，不在 MCU 能力。

---

## 4. 有没有必要？

### 4.1 值得做的场景

| 场景 | 说明 |
|------|------|
| 展会 / 演示灯效 | 现场改「A 红、B 蓝」无需重编 |
| 多用户偏好 | 每人一套 keymap，NVS 或按 device 配置 |
| 少固件变体 | 避免「Xbox 演示固件 / PS4 演示固件」各编一份（抽象键已统一，映射表再减业务分叉） |
| 教学 / 二次开发 | 网页改绑定比改 C++ 门槛低 |

### 4.2 短期可以不做的原因

| 原因 | 说明 |
|------|------|
| v0.1 运控键位仍在定 | [I04](./I04-apps-mapping.md) 多处 TBD；先稳定默认语义再开放 remap |
| 灯效已可动态改 | `led/cmd` 已能改颜色/模式；**缺的只是「按键触发」这一层胶水** |
| 编译期 App 更简单、更安全 | 狗控错误映射有安全风险；嵌入式量产常「出厂表 + 工程模式改」 |
| 与「一手柄一 App」痛点不完全等同 | I01 已反对「一手柄一页面」；物理差异在 Normalize。痛点是 **业务动作写死**，不是手柄型号 |

### 4.3 必要性结论

| 优先级 | 建议 |
|--------|------|
| **P0（现在）** | 契约已拍板（I08a）；固件尚未实现 |
| **P1（灯带演示有需求时）** | 做 **最小 keymap**：仅离散键 → `LedStripControl` 已有 Apply*；MQTT + NVS |
| **P2** | 扩展到舵机调试键；仍避开主运控 |
| **P3 / 谨慎** | 狗动作进 remap（需安全态、确认、限速） |

**有必要实现「可配置绑定」这一能力形态；没有必要在 v0.1 就上全量函数指针框架。** 灯带是最佳试点。

---

## 5. 嵌入式实践中常见吗？

**常见，而且几乎是标配思路**——只是名字不同：

| 领域 | 类似做法 |
|------|----------|
| 游戏 / 主机配件 | 键位 remap、十字键/摇杆交换；配置存 Flash |
| 消费电子遥控 | 学习码、场景键；红外/RF 码表在 NVS |
| 智能家居 / Matter | 按键 → 场景 / cluster command；云端或本地绑定 |
| PLC / HMI | 软按钮绑定变量或功能号 |
| 开源固件 | QMK/ZMK（键盘层与 keycode）；ESPHome `on_press` → action；Home Assistant automation |
| 本仓库近亲 | touch **阈值** NVS 可调；人脸 ID / Immich / MQTT broker 均 NVS——**配置可持久化已是 deep-dog 惯例** |

相对少见、且应避免的：

- 把函数地址当配置持久化  
- 在设备上解释任意脚本（Lua 全开）——除非产品明确要沙箱，成本高  

**「ID → 处理函数表 + 可写配置」是常规做法；你的直觉与业界一致。**

---

## 6. 方案对比

| 方案 | 改映射 | 安全/稳定 | 复杂度 | 建议 |
|------|--------|-----------|--------|------|
| A. 编译期 `HandleApp*`（现状） | 需重编 | 高 | 低 | **v0.1 默认** |
| B. action_id 表 + MQTT + NVS | 运行时 | 高（可校验） | 中 | **推荐增强** |
| C. NVS 存函数指针 | 运行时 | **低（OTA 崩）** | 低但错 | **否定** |
| D. 设备端 Lua/微服务脚本 | 极灵活 | 中低 | 高 | 非目标 |
| E. 仅云端/桥侧映射，设备收「已翻译命令」 | 运行时 | 高（设备傻） | 中（依赖在线） | 可作补充；离线灯控仍要 B |

推荐路径：**A 保底 + B 试点灯带**；PC 桥也可在 Python 侧先做「按键改写成 led/cmd」验证体验，再决定是否下沉固件 B。

---

## 7. 若落地：建议切片

1. **动作目录 v0**：仅 LED — `off / static / blink / breathe / scroll`，参数与 `led/cmd` 对齐。  
2. **键集合 v0**：`a,b,x,y`（可选 `l1,r1`）；仅 **press 边沿**。  
3. **Topic**：评估用 `handle/cmd` `action: set_keymap` + retain `handle/keymap`；**契约草稿见 [I08a](./I08a-keymap-mqtt-contract-draft.md)**；定稿后再写入 YAML 并同步 deep-trace。  
4. **App**：`HandleAppLedMap`；与 `HandleAppDog` 通过 profile 或 `handle/cmd` 切换，避免双驱动。  
5. **出厂默认**：与当前演示一致（如 A 红静态、B 蓝静态）；NVS 无数据则用默认。  
6. **验收**：Web 对换 A/B → 立即生效 → 断电重启仍对换 → 非法 id 回落默认且不崩。

不在本评估范围：摇杆曲线自定义、宏连招、多机同步 keymap。

---

## 8. 对原问题的直接回答

| 提问 | 回答 |
|------|------|
| 结构体函数指针，按 A/B 调不同函数？ | **可以**，作为 RAM 派发层。 |
| Web MQTT 改结构体实现对换？ | **可以**，但改的是 **action_id（及参数）**，不是指针数值。 |
| 存 NVS 固化 mapping？ | **可以且应该**；校验版本与 id 范围。 |
| 想法能否实现？ | **能**。 |
| 有无必要？ | **灯带/演示：有；全量运控：暂缓。** |
| 实际用得多吗？ | **用得多**（keymap / command id）；裸持久化函数指针用得少且危险。 |

---

## 9. 与现有文档的关系

| 文档 | 关系 |
|------|------|
| [I01](./I01-architecture.md) | 分层不变；动态映射落在 App，不进 Normalize |
| [I04](./I04-apps-mapping.md) | v0.1 仍为编译期表；本文为后续可选演进 |
| [08-led](../mqtt/modules/08-led.md) | 执行器已具备；keymap 是触发源之一 |
| [11-handle](../mqtt/modules/11-handle.md) | 若做 P1，再扩 cmd/status 字段 |
| [I08a](./I08a-keymap-mqtt-contract-draft.md) | MQTT 契约 **已拍板**（已合入 YAML） |
| ROADMAP | 未单独立项；有演示需求时再挂 **I-KEYMAP** 类序号 |

---

## 10. 验收（本文档）

- [x] 可行性结论明确（可行 + 存 ID 不存指针）
- [x] 必要性分场景（灯带 P1 / 运控暂缓）
- [x] 行业惯例说明
- [x] 与 I01/I04/led 架构对齐的落点建议
- [x] 契约草稿：[I08a](./I08a-keymap-mqtt-contract-draft.md) → **已拍板并合入 YAML**
- [ ] 固件实现 `HandleAppLedMap` + NVS + profile 门控
