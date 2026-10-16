# deep-dog 文档写作规范（CONVENTIONS）

本文件定义 **什么场景 → 写哪个目录 → 用什么命名 → 写什么格式**。所有 deep-dog 板级文档必须遵循本规范；宿主规则文件（`.trae/rules`、`.cursor/rules`）是强约束入口，本文件是详解与模板。

文档根：[`docs/`](./README.md)。

---

## 1. 分层模型

| 域 | 目录 | 回答什么问题 | 读者 |
|----|------|--------------|------|
| 需求 | `docs/requirements/` | 板子**要做什么**、交付顺序、MQTT 契约 | 固件 / 前端 / 产品 |
| 跨端契约 | `docs/interface/` | 设备与云平台**怎么对接**（MQTT/WS/HTTP） | 固件 / 后端 / 前端 |
| 经验教训 | `docs/ll/` | 踩过的坑**以后怎么避** | 全体开发者 |

原则：

- **单一真源**：同一事实只写一处，其它地方链接引用（例：MQTT 字段只认 `requirements/mqtt/protocol/deep-dog-mqtt.yml`）。
- **先文档后代码**：新功能先补/更新需求文档，再写实现（见 §4）。
- **板级隔离**：文档只描述 `main/boards/deep-dog/` 的板级内容。

---

## 2. 编号与命名规则

| 域 | 目录 | 命名 | 索引 |
|----|------|------|------|
| 需求总表 | `requirements/ROADMAP.md` | 序号 + ID（D / V-S / V-C / M / N / MOT / DT / CFG / DP…） | 自身 |
| MQTT | `requirements/mqtt/` | `M01`、`M02`；`modules/NN-name.md`；`protocol/deep-dog-mqtt.yml` | `mqtt/README.md` |
| 视觉 Server | `requirements/vision/server/` | `SNN-*.md`（S01…） | `vision/server/README.md` |
| 视觉 Client | `requirements/vision/client/` | `CNN-*.md`（C01…） | `vision/client/README.md` |
| 视觉 Product | `requirements/vision/product/` | `PNN-*.md`（P01…） | `vision/product/README.md` |
| 输入 | `requirements/input/` | `INN-*.md`（I01…，含 I08a/I08b） | `input/README.md` |
| 电机/CAN | `requirements/motor/`（+`teaching/`） | `NN-*.md` | `motor/README.md` |
| 网络 | `requirements/net/` | `NNN-*.md`（N01…） | `ROADMAP.md` 挂接 |
| 配置中心 | `requirements/config/` | `CFG-NN-*.md` | `config/README.md` |
| 四足运动 | `requirements/dog/` | `DEVELOPMENT_PLAN.md` | `dog/README.md` |
| WS-MCP | `requirements/ws-mcp/` | `NN-*.md` | `ws-mcp/README.md` |
| 架构图集 | `requirements/architecture/` | `*.html`（Mermaid） | `index.html` |
| 跨端契约 | `interface/` | `NN-<topic>-contract.md` | `interface/README.md` |
| 经验教训 | `ll/` | `LL-NNN-<slug>.md` | `ll/README.md` |
| **功能/用例注册表** | `requirements/catalog/` | `F<NNNN>-<domain>-<slug>` / `TC<NNNN>-<domain>-<slug>`（见 §2.1） | `catalog/FEATURES.md` · `catalog/TESTS.md` |

- 序号三位、全局递增，**不复用旧号**；文件夹只表归属，不代表顺序。
- 文件名用 ASCII，正文用中文，命令用代码块。

---

## 2.1 功能需求编号（F）与测试用例编号（TC）

> 目标：给**每一个功能点**一个稳定 ID，并让「功能点 ↔ 测试用例 ↔ 自动化脚本 ↔ 测试结果」四者可互相追溯。
> 编号层与既有「路线图 ID」（M01 / V-S03 / MOT-01 / I08b / CFG-01 …）**并存不冲突**：
> 路线图 ID 表**交付批次**，F/TC 表**可验证的最小功能单元**。两者在注册表里互相映射。

### 命名格式

| 类型 | 格式 | 示例 | 注册表（唯一真源） |
|------|------|------|--------------------|
| 功能点 | `F<NNNN>-<domain>-<slug>` | `F1050-motor-enable` | [`requirements/catalog/FEATURES.md`](./requirements/catalog/FEATURES.md) |
| 测试用例 | `TC<NNNN>-<domain>-<slug>` | `TC1050-motor-enable` | [`requirements/catalog/TESTS.md`](./requirements/catalog/TESTS.md) |

规则：

1. `F<NNNN>`：**4 位十进制、全局唯一、只增不复用**；按域在预留号段内顺序分配（见下表）。
2. `TC<NNNN>`：默认与所测 `F<NNNN>` **同号**（最常见 1:1）；一个功能点需要多组用例时，用**字母后缀**扩展（`TC1050a`/`TC1050b`），不额外占号。
3. `<domain>`：取自下方**固定词表**，全小写；**禁止自造**。一个功能点只归一个主域。
4. `<slug>`：小写 kebab-case ASCII，简短可读；同一域内不重复。
5. 每个 `F` 必须至少指回一份需求文档（`出处` 列）；每个 `TC` 必须写明 `预期` 与 `判据`（`自动` / `串口` / `人工`）。
6. 缩写约定：`slug` 里用 `cfg`=配置、`mqtt`=MQTT 通道、`http`=HTTP 通道、`mcp`=MCP 工具、`dp`=数据点。

### 域词表与号段

| domain | 号段 | 覆盖范围 |
|--------|------|----------|
| `device` | F0001–F0099 | 设备信息 / 心跳 / 内存 / 诊断 / 系统 |
| `pair` | F0100–F0199 | 设备配对（MQTT 配对 + HTTP 配对） |
| `net` | F0200–F0299 | SNTP / STA 寻址 / 配网页 broker |
| `config` | F0300–F0399 | 统一配置中心 / 38·48 引出脚运行时模式 |
| `stream` | F0400–F0499 | MQTT 推流 / MJPEG / 拍照 / 采集模式 |
| `face` | F0500–F0599 | 人脸检测 / 识别 / 人脸库 / 人脸 MCP |
| `immich` | F0600–F0649 | Immich 真名绑定 / 素材策略 |
| `greet` | F0650–F0679 | 主动招呼 / `person/active` / 当前说话人 |
| `track` | F0680–F0699 | 人脸跟踪 |
| `touch` | F0700–F0749 | 触摸按键 / 标定 / 组合手势 |
| `handle` | F0750–F0799 | 手柄输入 / 键位映射 / 轴映射 |
| `led` | F0800–F0849 | WS2812 灯带 |
| `gimbal` | F0850–F0899 | 云台 pan/tilt |
| `servo` | F0900–F0949 | 裸舵机 |
| `imu` | F0950–F0999 | IMU 六轴 / 动作开关 |
| `can` | F1000–F1049 | CAN 总线 / 透传 / 帧注入 |
| `motor` | F1050–F1149 | 单电机 / 示教录制回放 |
| `dog` | F1150–F1249 | 四足运控 / 步态 / 姿态 |
| `dp` | F1250–F1299 | 数据点归一化（Registry / 4 通道） |
| `mcp` | F1300–F1349 | 局域网 WS-MCP 桥 |
| `http` | F1350–F1399 | 内置 HTTP server / HTTP 契约 |
| `rtsp` | F1400–F1449 | RTSP / MediaMTX / 流媒体客户端 |

> 号段用尽时**新增号段**（追加到表尾），不复用历史号段。

### 状态取值（`FEATURES.md` / `TESTS.md` 通用）

`已实现` · `部分实现` · `占位` · `待办`（文档有/代码无） · `未见`（仅契约）·
测试结果：`PASS` · `FAIL` · `SKIP`（附原因） · `BLOCKED`（附阻塞项）。

---

## 3. 场景 → 目录 → 格式

### 3.1 经验教训（Lessons Learned）

**何时写**（满足任一条）：排查耗时长、结论非显而易见；复现成本高（编译/烧录/硬件/环境）；属配置/环境/流程类而非一次性笔误；有明确「下次该怎么做」。

- 落到：`docs/ll/LL-NNN-<slug>.md`
- 必备结构：

```markdown
## LL-NNN 一句话标题

- **现象**：看到的日志 / 报错 / 表现
- **根因**：一句话说清为什么
- **正确做法**：
  ```bash
  # 可直接照做的命令或改法
  ```
- **相关文件**：`path/to/file`
```

- 写完在 `ll/README.md` 索引加一行；**禁止**写密码 / Token / 密钥。

### 3.2 新需求 / 功能变更（需求先行）

**何时写**：通过对话或其它渠道收到新需求、功能改动时，**先**确认/补齐需求文档，**再**改代码。

- 落到：`docs/requirements/<域>/`（无匹配子域时先规划路径，沿用同域编号风格）
- 必备内容：**目标 / 边界（含非目标）/ 方案 / 验收 / 依赖**，并挂到域 README + `ROADMAP.md`
- 涉及 MQTT 时：同步更新 `requirements/mqtt/protocol/deep-dog-mqtt.yml` 与对应 `modules/`
- 模板：

```markdown
# <ID> · <标题>

| 项 | 内容 |
|----|------|
| 路线图 ID | <V-Sxx / MOT-xx / …> |
| 状态 | 需求就绪 / 实现中 / 已落地 |
| 依赖 | <上游文档或模块> |

## 目标
## 边界（非目标）
## 方案 / 字段
## 验收
## 依赖与挂接
```

### 3.3 跨端接口契约（MQTT / WS / HTTP）

**何时写**：设备与云平台之间的主题、字段、接口、错误码、生命周期发生变化。

- 落到：`docs/interface/`
- 命名：`NN-<topic>-contract.md`
- 必备内容：统一命名空间 / 字段规范 / 错误码体系 / 请求-响应流程 / 心跳重连 / 版本兼容规则
- **流程**：先改契约文档 → 双端评审 → 再改码；新增字段必须可选，改名/删除走弃用期
 
### 3.4 MQTT 模块前端契约

- 落到：`docs/requirements/mqtt/modules/NN-<name>.md`
- 必备内容：Topic 表 / 字段表（名称·类型·必填·说明）/ 前端 Steps / 固件实现 / 验收
- 字段真源同步 `mqtt/protocol/deep-dog-mqtt.yml`

### 3.5 架构总览 / 机理解析

- 落到：`docs/requirements/architecture/*.html`（HTML + Mermaid），从宏观到机制分层
- 字段真源不复制，指向 YAML

### 3.6 交付顺序 / 状态

- 落到：`docs/requirements/ROADMAP.md`，只维护**序号 / ID / 文档 / 依赖 / 状态 / 验收指针**

### 3.7 板级开发说明（模块 README）

- 落到各源码模块目录（如 `motor/README.md`）：模块职责 / 对外接口 / 指向 `docs/requirements/` 的需求出处

---

## 4. 强制流程（需求先行）

1. **检索**：在 `docs/requirements/` 下按关键词、模块 ID、ROADMAP 序号查找是否已有需求。
2. **无匹配 → 先文档后代码**：规划路径 → 新建/更新需求文档（§3.2）→ 涉及 MQTT 同步 YAML 与 modules → 再写实现。
3. **有匹配 → 对齐后开发**：文档与输入不一致时先更新文档，再按文档实现。
4. **出处可追溯**：动手改代码前，在回复中写明本次功能对应的需求文档路径；每个功能点都能指回至少一份文档。

禁止：
- 未确认/未补齐需求文档就直接实现（含「先写代码回头补文档」）；
- 只改代码不落需求；
- 扩大改动面到未写入 `ROADMAP.md` / 需求文档的范围。

---

## 5. 通用约束

- **单一真源**；跨仓引用一律**绝对路径**；**禁止**写入密码 / Token / 密钥。
- 新增文档必须挂到所属域 README，必要时同步 `ROADMAP.md`。
- 跨端契约变更后，按 `.trae/rules/deep-dog-sync-frontend-reqs.md` 同步 deep-trace 前后端需求。
