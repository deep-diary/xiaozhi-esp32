# MCP 工具归一化规范（MCP Tool Normalization）

> 归属：`docs/interface/` 跨端接口契约 · 契约版本 `interop-2`
> 关联：[04-datapoint-registry.md](./04-datapoint-registry.md)（数据点体系）、
> [02-ws-contract.md](./02-ws-contract.md)（设备 WS-MCP）
> 适用：deep-dog 板级所有 `self.*` MCP 工具的新增、修改与审计

---

## 1. 目的

数据点归一化（见 04）之后，**标量读写**可由数据点体系**自动生成** MCP 工具：

- `self.<module>.get_<point>`（读）
- `self.<module>.set_<point>`（写，经统一校验引擎）
- 子实例：`self.<module>.get_<sub>_<point>`（如 `self.motor.get_1_position_rad`）

为避免"同一能力在数据点与手写代码里各注册一份"造成**真源分裂**与**工具冗余**，
本规范确立二者**职责边界**与**新增冻结规则**。

---

## 2. 铁律（职责边界，强约束）

| 能力类型 | 唯一入口 | 说明 |
|----------|----------|------|
| **标量读**（bool/int/float/string/enum） | **数据点体系** | `self.<module>.get_<point>`，registry 自动生成 |
| **标量写** | **数据点体系** | `self.<module>.set_<point>`，统一校验（类型/范围/枚举/权限） |
| **过程性操作**（动作 / 流程 / 扫描 / 录制 / 配对 / 整机姿态 / 效果） | **模块手写** | 不进数据点体系（见 04 §3.5） |
| **结构体 / 数组的整体快照** | **模块手写** | 一次调用返回整体，优于 N 次单点调用 |

**判据一句话**：

- **"一个值"→ 数据点**；**"一个动作 / 一段流程"→ 手写工具**。
- 当手写工具与数据点**形态不同但具备形态优势**（如整体快照、批量效果）时，**保留手写**，不强行收敛。

---

## 3. 新增 MCP 工具决策流程

```
要新增 MCP 工具？
  │
  ├─ 它只是"读/写单个标量"吗？
  │     └─ 是 → ❌ 不要手写 AddTool
  │             ✅ 改为在对应模块的 <module>_datapoints.cc 声明数据点
  │                （自动获得 get/set + MQTT + HTTP + schema + telemetry）
  │
  └─ 否（动作 / 流程 / 整体快照 / 效果）
        └─ ✅ 手写 mcp_server.AddTool(...)，命名遵循 §4
```

---

## 4. 命名规范

| 类别 | 格式 | 示例 |
|------|------|------|
| 数据点自动（单实例） | `self.<module>.get_<point>` / `set_<point>` | `self.device.get_ip`、`self.led.set_brightness` |
| 数据点自动（子实例） | `self.<module>.get_<sub>_<point>` | `self.motor.get_1_position_rad` |
| 手写过程性操作 | `self.<domain>.<verb>[_<obj>]` | `self.dog.stand`、`self.motor.scan_bus`、`self.face.set_mode` |

- `domain` 取模块名（`dog` / `chassis` / `leg` / `motor` / `can` / `face` / `servo` / …）。
- `verb` 用动词（`init` / `stand` / `scan` / `set_mode` / `start_recording` / …）。
- **禁止**手写 `get_*` / `set_*` 形式去**镜像一个已存在的数据点**。

---

## 5. 现有工具分类（基线快照，2026-10）

| 模块 | 工具（前缀 / 代表） | 性质 | 处置 |
|------|--------------------|------|------|
| dog / chassis | `self.dog.*`、`self.chassis.*`（init/stand/pose/dance…） | 过程性操作 | 保留（手写） |
| leg | `self.leg.*`（init/stand/step_forward/step_back） | 过程性操作 | 保留（手写） |
| motor | `self.motor.*`（scan_bus/set_*_mode/initialize/recording/…） | 过程性操作 | 保留（手写） |
| can | `self.can.*`（send_motor_position/…/control_motor） | 过程性操作 | 保留（手写） |
| face | `self.face.*`（set_mode/manage/list/…） | 过程性操作 | 保留（手写） |
| pairing | `self.device.*`（start_pairing/cancel_pairing/unbind） | 过程性操作 | 保留（手写） |
| servo | `self.servo.*`（set_angle/set_type/attach/detach） | 过程性操作 | 保留（手写） |
| servo | `self.servo.get_status` | **整体快照**（一次返回双舵机） | 保留（形态优于 14 次单点） |
| motor | `self.motor.get_status`、`self.motor.get_angle_status` | 整体快照 | 保留 |
| led | `self.led_strip.*`（blink/scroll/breathe/show_device_state） | 效果 / 过程性 | 保留（手写） |
| led | `self.led_strip.get_brightness` / `set_brightness` | 标量，但**语义不同**（level 0-8 vs 数据点 raw 0-255） | 保留（见 §8） |
| board | `self.board.get_ip` | 与数据点 `device.ip` **等价** | 保留（见 §8） |
| board | `self.board.diagnostics` | 诊断（过程性） | 保留（手写） |

> 数据点自动工具（`self.device.*` / `self.system.*` / `self.imu.*` / `self.can.get_*` 等）
> 由 [04-datapoint-registry.md](./04-datapoint-registry.md) 的 registry 在运行期生成，
> 不在本表逐一列出。

---

## 6. 冻结规则与运行时保障

- **冻结**：自本规范生效起，**标量读写一律经数据点体系**；不再新增"镜像数据点"的手写工具。
- **运行时保障**：`McpServer::AddTool` 已内建**重名检测**——重复工具名会被拒绝并打印
  `Tool %s already added` 警告。因此若有人误加与数据点自动工具同名的工具，会被自动拦下并告警。
  （实现：`main/mcp_server.cc` `McpServer::AddTool(McpTool*)`）

---

## 7. 接口审计清单（新增 / 修改 MCP 工具必须逐条通过）

| 检查项 | 要求 |
|--------|------|
| 职责归类 | 标量读写 → 数据点；动作 / 流程 / 整体快照 → 手写（§2） |
| 命名 | 符合 §4；手写工具不含 `get_*` / `set_*` 镜像形式 |
| 重名 | 不与数据点自动工具名冲突（§6 保障） |
| 可发现性 | 标量能力应在 `info/schema` 中可见（即经数据点声明） |
| 真源唯一 | 同义字段不在两处各自硬编码（对齐 [00 §4.7](./00-architecture-review.md) 审计清单） |

---

## 8. 渐进收敛（后续可选项，当前**保留**）

以下工具与数据点存在**部分**重叠，但按 §2 判据当前**保留**；若未来确需收敛，须走契约变更流程，
并评估对 LLM 提示词 / 前端的影响：

| 工具 | 与数据点的关系 | 保留原因 |
|------|---------------|----------|
| `self.board.get_ip` | 等价 `device.ip` | 避免破坏既有提示词/前端调用 |
| `self.led_strip.get_brightness` / `set_brightness` | 语义不同（level 0-8 对数映射 vs raw 0-255） | 语义不等价，直接替换会改变交互 |
| `self.servo.get_status` | 覆盖数据点 `servo.<i>.*` | 整体快照，一次调用优于多次单点 |
| `self.motor.get_status` / `get_angle_status` | 覆盖数据点 `motor.<id>.*` | 同上 |

---

## 9. 验收

- [ ] 新增标量能力均以数据点声明，未新增手写镜像工具
- [ ] 手写工具命名符合 §4，且不与数据点自动工具重名
- [ ] 本规范与 [04-datapoint-registry.md](./04-datapoint-registry.md) 的职责边界一致
- [ ] 现有手写工具（§5）功能未受影响（本规范为**零删除**，不动现有工具）
