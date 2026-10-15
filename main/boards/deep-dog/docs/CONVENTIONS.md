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

- 序号三位、全局递增，**不复用旧号**；文件夹只表归属，不代表顺序。
- 文件名用 ASCII，正文用中文，命令用代码块。

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
