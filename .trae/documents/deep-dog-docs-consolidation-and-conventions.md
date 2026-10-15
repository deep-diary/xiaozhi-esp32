# deep-dog 文档体系整理 + 写作规则定义（实施计划）

> 范围：仅 `main/boards/deep-dog/**` 及仓库根 `.trae/rules`、`.cursor/rules`、`AGENTS.md`、`scripts/deep_dog/**`。
> 目标：把 `deep-dog/swrs/` 合并进 `deep-dog/docs/`，使 `docs/` 成为**板级开发文档唯一汇总根**，并定义「场景 → 目录 → 命名 → 格式」规则。

---

## 1. 摘要（Summary）

三件事一起做：

1. **目录合并**：`main/boards/deep-dog/swrs/` 整体迁移为 `main/boards/deep-dog/docs/requirements/`。
2. **新增规范文档**：`docs/README.md`（文档总枢纽 / 场景路由表）与 `docs/CONVENTIONS.md`（各类型文档的格式与模板）。
3. **全量修引用**：仓库内所有引用 `swrs/...` 的相对链接、代码注释、脚本路径、YAML、HTML 统一改到新路径，做到**零死链**；`.trae/rules` 与 `.cursor/rules` 规则文件同步更新（含 `deep-dog-swrs-first` → `deep-dog-docs-first` 重命名）。

---

## 2. 现状分析（Current State Analysis）

### 2.1 两个并存的文档根

| 目录 | 内容 | 定位 |
|------|------|------|
| `deep-dog/swrs/` | `README.md`、`ROADMAP.md`、`AUDIT_REPORT.md`、`architecture/`、`mqtt/`、`vision/`、`motor/`、`input/`、`net/`、`config/`、`dog/`、`ws-mcp/` | 板内**需求 + 路线图 + MQTT 契约 + 架构图** |
| `deep-dog/docs/` | `interface/`（跨端契约，含 `00-architecture-review`、`01-mqtt-contract`、`02-ws-contract`、`03-http-api-contract`、`04-datapoint-registry`）、`ll/`（经验教训 `LL-001`~`LL-004`） | 跨端契约 + 经验教训 |

两者互相引用、边界清晰，但**物理上分散**，不符合「docs 作为板级文档汇总根」的目标。

### 2.2 现有编号 / 命名约定（迁移后保持不变）

| 域 | 目录 | 命名风格 | 索引文件 |
|----|------|----------|----------|
| 需求总表 | `requirements/ROADMAP.md` | 序号 + ID（D/V-S/V-C/M/N/MOT/DT/CFG/DP…） | 自身 |
| MQTT 契约 | `requirements/mqtt/` | `M01`、`M02`；`modules/NN-name.md`；`protocol/deep-dog-mqtt.yml` | `mqtt/README.md` |
| 视觉 Server | `requirements/vision/server/` | `SNN-*.md`（S01…S09） | `vision/server/README.md` |
| 视觉 Client | `requirements/vision/client/` | `CNN-*.md`（C01…C05） | `vision/client/README.md` |
| 视觉 Product | `requirements/vision/product/` | `PNN-*.md`（P01…P02） | `vision/product/README.md` |
| 输入 | `requirements/input/` | `INN-*.md`（I01…I09，含 I08a/I08b） | `input/README.md` |
| 电机/CAN | `requirements/motor/`（+`teaching/`） | `NN-*.md` | `motor/README.md` |
| 网络 | `requirements/net/` | `NNN-*.md`（N01…N03） | 无（ROADMAP 挂接） |
| 配置中心 | `requirements/config/` | `CFG-NN-*.md` | `config/README.md` |
| 四足运动 | `requirements/dog/` | `DEVELOPMENT_PLAN.md` | `dog/README.md` |
| WS-MCP | `requirements/ws-mcp/` | `NN-*.md` | `ws-mcp/README.md` |
| 架构图集 | `requirements/architecture/` | `*.html`（Mermaid） | `index.html` |
| 经验教训 | `docs/ll/` | `LL-NNN-*.md` | `ll/README.md` |
| 跨端契约 | `docs/interface/` | `NN-*-contract.md` / `04-datapoint-registry.md` | `interface/README.md` |

### 2.3 引用面（决定工作量）

- 引用 `swrs` 的文件 **54 个**，在 `*.md` 内共 **65 处**、非 md 文件 **27 处**。
- **迁移子树内部**相对链接不变（源与目标一起移动）；需要改的只有**逃出子树**的链接：
  - 指向板根（`deep-dog/config.h`、`handle/`、`mqtt/`、`FEATURE_FLAGS.md`…）
  - 指向仓库根（`scripts/`、`partitions/v2/`、`docs/mcp-usage_zh.md`、`deep-thumble/`、`otto-robot/`、`deep-trace/`）
  - 指向 `docs/interface/`（如 `../docs/interface/…` → `../interface/…`）
- `swrs/**` 内 `](../../` 命中 138 处、`](../` 命中 156 处（含不逃逸的，需按文件深度甄别）。
- 另有 **陈旧引用**：`swrs/interop/04-datapoint-registry.md`（在 `http-server/datapoint_http.h`、`ws-mcp/datapoint_mcp.h`、`mqtt/modules/led_datapoints.h`），实际文档在 `docs/interface/04-datapoint-registry.md`——顺便纠正。

---

## 3. 目标文档体系（Target Structure）

```
main/boards/deep-dog/
├── README.md                      # 板级入口（更新“板级目录结构”+ 链接）
├── FEATURE_FLAGS.md
└── docs/                          # ★ 板级文档唯一汇总根
    ├── README.md                  # 【新增】文档总枢纽：三大域 + 场景路由表 + 索引
    ├── CONVENTIONS.md             # 【新增】文档规范：场景→目录→命名→格式 + 模板
    ├── requirements/              # 【由 swrs/ 整体迁入】板内需求 / 路线图 / MQTT 契约 / 架构图
    │   ├── README.md  ROADMAP.md  AUDIT_REPORT.md
    │   ├── architecture/
    │   ├── mqtt/  vision/  motor/  input/  net/  config/  dog/  ws-mcp/
    ├── interface/                 # 跨端契约（位置不变，更新指向 requirements 的链接）
    └── ll/                        # 经验教训（位置不变，更新指向 requirements 的链接）
```

### 3.1 场景 → 文档 → 格式 路由表（写入 `docs/CONVENTIONS.md`）

| 场景 | 落盘目录 | 命名 | 必备内容 / 格式 |
|------|----------|------|------------------|
| **① 经验教训**（bug 排查耗时、结论可复用） | `docs/ll/` | `LL-NNN-<slug>.md`（三位全局递增） | 现象 / 根因 / 正确做法（可照做命令）/ 相关文件；更新 `ll/README.md` 索引；禁写密钥 |
| **② 新需求 / 功能变更（需求先行）** | `docs/requirements/<域>/` | 沿用域内编号（`mqtt/modules/NN-*`、`vision/server/SNN-*`、`input/INN-*`…） | 目标 / 边界（非目标）/ 验收 / 依赖 / 与域 README + `ROADMAP.md` 挂接；涉及 MQTT 同步 `deep-dog-mqtt.yml` 与对应 modules |
| **③ 跨端接口（MQTT/WS/HTTP）契约** | `docs/interface/` | `NN-<topic>-contract.md` | 命名空间 / 字段规范 / 错误码 / 请求-响应流程 / 心跳重连 / 版本兼容；变更先改契约后改码 |
| **④ MQTT 模块前端契约** | `docs/requirements/mqtt/modules/` | `NN-name.md` + `protocol/deep-dog-mqtt.yml` | Topic / 字段表 / 前端 Steps / 固件实现 / 验收 |
| **⑤ 架构总览 / 机理解析** | `docs/requirements/architecture/` | `*.html`（Mermaid） | 宏观→机制分层；字段真源指向 YAML |
| **⑥ 交付顺序 / 状态追踪** | `docs/requirements/ROADMAP.md` | — | 序号 / ID / 文档 / 依赖 / 状态 / 验收指针 |
| **⑦ 板级开发说明（模块 README）** | 各源码模块目录（如 `motor/README.md`） | `README.md` | 模块职责 / 对外接口 / 指向 `docs/requirements` 的需求出处 |

> 约束：同一事实只有**一个真源**（MQTT 字段真源 = `mqtt/protocol/deep-dog-mqtt.yml`）；跨仓引用一律**绝对路径**；新增文档必须挂到域 README + `ROADMAP.md`。

---

## 4. 待变更清单（Proposed Changes）

### 4.1 新增文件

| 文件 | 内容要点 |
|------|----------|
| `main/boards/deep-dog/docs/README.md` | 定位一句话；目录树（requirements / interface / ll）；**场景路由表**（§3.1 精简版，指向 `CONVENTIONS.md`）；三大域索引链接；约定（单一真源、编号、禁敏感信息） |
| `main/boards/deep-dog/docs/CONVENTIONS.md` | §3.1 完整路由表；各类型**模板**（需求 / 契约 / MQTT 模块 / 经验教训 / 架构图）；编号与命名规则汇总（§2.2）；索引挂接要求；禁止项；与 `.trae`/`.cursor` 规则的关系说明 |

### 4.2 目录迁移

- `git mv main/boards/deep-dog/swrs main/boards/deep-dog/docs/requirements`
- 迁移后 `docs/requirements/README.md` 即原 `swrs/README.md`。
- 不保留旧 `swrs/` 占位目录（无兼容 shim）。

### 4.3 引用更新（全量，零死链）

**A. 迁移子树内部链接重写算法**（对 `docs/requirements/**` 下全部 `.md`）：
1. 记源文件在原 `swrs/` 内的深度 `d`（`swrs/README.md`→0；`swrs/input/x.md`→1；`swrs/mqtt/modules/x.md`→2；`swrs/motor/teaching/x.md`→2…）。
2. 相对链接中含 `k` 个 `../`：
   - `k ≤ d`：**不逃逸**，保持不变。
   - `k > d`：**逃逸**，`../` 数量 **+1**。
3. 特例：指向 `docs/interface` 的 `../docs/interface/…` → `../interface/…`（`../docs/` 段替换为 `../`）。
4. 外部绝对路径（`/Volumes/...`）不改。
- 涉及文件（50 个，命中 138 处 `](../../`）：`swrs/README.md`、`ROADMAP.md`、`AUDIT_REPORT.md`、`dog/DEVELOPMENT_PLAN.md`、`mqtt/M02-*.md`、`mqtt/modules/*`、`mqtt/frontend/*`、`vision/server/S01/S06/S07/S08/S09*`、`vision/client/C02/C03/C05/README`、`input/I01/I02/I03/I06/I08`、`motor/01/03/06/07/09`、`motor/teaching/*`、`net/N02/N03`、`ws-mcp/*`、`config/CFG-02` 等（以实际 grep 结果为准）。

**B. 板内源码目录 README / 根文档（`swrs/` → `docs/requirements/`）**

| 文件 | 行 | 变更 |
|------|----|------|
| `main/boards/deep-dog/README.md` | L67（目录结构块）、L169 | `swrs/` → `docs/requirements/`；结构块补 `docs/` 汇总说明 |
| `CAN/../can/README.md` `can/README.md` | L125 | `../swrs/mqtt/modules/12-can.md` → `../docs/requirements/mqtt/modules/12-can.md` |
| `motor/README.md` | L37、L45 | `../swrs/motor/...` → `../docs/requirements/motor/...` |
| `servo/README.md` | L43 | 同上（`10-servo.md`） |
| `led/README.md` | L18 | 同上（`08-led.md`） |
| `gimbal/README.md` | L30 | 文中 `swrs/mqtt/modules/09-gimbal.md` → `docs/requirements/...` |
| `http-server/README.md` | L62 | `../swrs/ROADMAP.md` → `../docs/requirements/ROADMAP.md` |
| `ws-mcp/README.md` | L41 | `../swrs/ws-mcp/01-...md` → `../docs/requirements/ws-mcp/...` |
| `vision/README.md` | L40 | `../swrs/vision/fixtures/...` → `../docs/requirements/vision/fixtures/...` |
| `touch_btn/README.md` | L87、130、133、141、150 | `../swrs/mqtt/modules/...` → `../docs/requirements/mqtt/modules/...` |
| `touch_btn/COMBO_EVAL.md` | L5 | 同上 |
| `handle/sources/README.md` | L4、11、12 | `../../swrs/input/...` → `../../docs/requirements/input/...` |
| `face_ai/README.md` | L33、51 | `../swrs/vision/...` → `../docs/requirements/vision/...` |
| `CAMERA_SENSOR.md` | L128、210、242 | `swrs/vision/...` → `docs/requirements/vision/...` |
| `FEATURE_FLAGS.md` | L33、98 | `./swrs/config/...`、`./swrs/vision/infra.md` → `./docs/requirements/...` |
| `scripts/deep_dog/README.md` | L59 | `../../main/boards/deep-dog/swrs/input/...` → `.../docs/requirements/input/...` |

**C. `docs/interface/**` 内指向 `swrs/` 的链接/文本**

| 文件 | 行 | 变更 |
|------|----|------|
| `docs/interface/README.md` | L6、32 | `../../swrs/README.md`→`../requirements/README.md`；`../../swrs/mqtt/README.md`→`../requirements/mqtt/README.md`；`../../swrs/mqtt/protocol/deep-dog-mqtt.yml`→`../requirements/mqtt/protocol/deep-dog-mqtt.yml` |
| `docs/interface/01-mqtt-contract.md` | L4、92 | 同上（YAML 路径 + 文本 `swrs/mqtt/...`） |
| `docs/interface/02-ws-contract.md` | L132 | 文本 `swrs/mqtt/M01:39` → `requirements/mqtt/M01:39` |
| `docs/interface/03-http-api-contract.md` | L17、158 | 文本/链接 `swrs/...` → `docs/requirements/...` |
| `docs/interface/04-datapoint-registry.md` | L638、639 | `../../swrs/mqtt/M01...`、`../../swrs/mqtt/protocol/deep-dog-mqtt.yml` → `../requirements/mqtt/...` |
| `docs/interface/00-architecture-review.md` | L28、269 | 文本 `swrs/mqtt/**`、`input/**`、`vision/infra.md` → `requirements/...` |

**D. `docs/ll/**`**

| 文件 | 行 | 变更 |
|------|----|------|
| `docs/ll/LL-004-restore-headers-and-union-size-measure.md` | L22 | `../swrs/config/CFG-02-...md` → `../requirements/config/CFG-02-...md` |

**E. 源码注释（`swrs/...` → `docs/requirements/...`；陈旧路径一并纠正）**

| 文件 | 行 | 变更 |
|------|----|------|
| `config.h` | L27 | `swrs/motor/01-can-hw-profile.md` → `docs/requirements/motor/01-can-hw-profile.md` |
| `net/net_config.h` | L9、34 | `swrs/net/N02`、`N01` → `docs/requirements/net/...` |
| `net/deep_dog_sntp.h` | L10 | 同上（N01） |
| `mqtt/mqtt_config.h` | L63、108、110 | `swrs/net/N03`、`swrs/mqtt/modules/16-http-pairing`、`15-telemetry` → `docs/requirements/...` |
| `mqtt/mqtt_config.cc` | L41 | `swrs/mqtt/modules/00-pairing` → `docs/requirements/mqtt/modules/00-pairing` |
| `mqtt/modules/telemetry_mqtt.h` | L10 | `swrs/mqtt/modules/15-telemetry.md` → 新路径 |
| `mqtt/modules/pairing_mqtt.cc` | L277 | `swrs/mqtt/modules/16-http-pairing` → 新路径 |
| `esp_sparkbot_board.cc` | L907 | `swrs/mqtt/M01` → `docs/requirements/mqtt/M01` |
| `pairing/deep_dog_pair_http.h` | L8 | `swrs/mqtt/modules/16-http-pairing.md` → 新路径 |
| `ws-mcp/datapoint_mcp.h` | L9 | `swrs/interop/04-datapoint-registry.md §3.5` → `docs/interface/04-datapoint-registry.md §3.5`（纠错） |
| `http-server/datapoint_http.h` | L6 | `swrs/interop/04-datapoint-registry.md §3.4` → `docs/interface/04-datapoint-registry.md §3.4`（纠错） |
| `mqtt/modules/led_datapoints.h` | L9 | `swrs/interop/04-datapoint-registry.md` → `docs/interface/04-datapoint-registry.md`（纠错） |

**F. 脚本 / YAML / HTML**

| 文件 | 行 | 变更 |
|------|----|------|
| `scripts/deep_dog/deep_dog_rtsp_pull_verify.py` | L29 | `DEFAULT_OUT = ROOT / "main/boards/deep-dog/swrs/vision/fixtures/rtsp_pull_verify"` → `.../docs/requirements/vision/fixtures/rtsp_pull_verify` |
| `main/boards/deep-dog/swrs/mqtt/protocol/deep-dog-mqtt.yml` | L149、152、177、358、925 | 文本/`ref` 内 `swrs/input/`、`swrs/motor/04`、`swrs/net/N01` → `docs/requirements/...` |
| `main/boards/deep-dog/swrs/architecture/*.html` | `index.html:29`、`components.html:117/159`、`signal_flow.html:62`、`detail.html:212`、`mechanisms.html:112` | 文案/图上 `swrs/mqtt/...`、`swrs/mqtt YAML`、`swrs/` → `docs/requirements/...` |

### 4.4 规则文件与仓库元信息

| 文件 | 变更 |
|------|------|
| `.trae/rules/deep-dog-swrs-first.md` | **重命名** → `.trae/rules/deep-dog-docs-first.md`；正文 `swrs/` → `docs/requirements/`；新增一句指向 `docs/README.md` 与 `docs/CONVENTIONS.md`；标题「需求文档先行（Docs First / Requirements First）」 |
| `.trae/rules/deep-dog-sync-frontend-reqs.md` | `globs` 中 `main/boards/deep-dog/swrs/**/*` → `main/boards/deep-dog/docs/requirements/**/*`；正文/示例绝对路径 `.../deep-dog/swrs/` → `.../deep-dog/docs/requirements/`；表内「设备契约 / swrs」→「设备契约 / docs/requirements」 |
| `.trae/rules/deep-dog-lessons-learned.md` | 记录位置补充「格式与模板见 `docs/CONVENTIONS.md`」；`docs/ll/` 路径不变 |
| `.cursor/rules/deep-dog-swrs-first.mdc` | **重命名** → `.cursor/rules/deep-dog-docs-first.mdc`，内容同步（镜像 .trae） |
| `.cursor/rules/deep-dog-sync-frontend-reqs.mdc` | 同步 .trae 内容（globs + 绝对路径） |
| `AGENTS.md` | L95、96、102 及「定制板: deep-dog」段：`swrs/` → `docs/requirements/`；补一句「板级文档根：`main/boards/deep-dog/docs/`（规范见 `docs/CONVENTIONS.md`）」 |
| `main/boards/deep-dog/README.md` | §4.3-B 已列；另在「板级目录结构」块补 `docs/` 说明 |

---

## 5. 假设与决策（Assumptions & Decisions）

1. **目录结构**：采用「`docs/` 分层 = `requirements/` + `interface/` + `ll/`」；原 `swrs` 整树迁入 `docs/requirements/`（用户已确认）。
2. **规则载体**：新增 `docs/README.md` + `docs/CONVENTIONS.md`，并同步 `.trae`/`.cursor` 规则（用户已确认）；规则文件是强约束入口，`CONVENTIONS.md` 是详解与模板。
3. **引用**：全量更新，**零死链**（用户已确认）；不保留 `swrs/` 兼容占位。
4. **编号体系**：迁移后各域编号风格**不变**，避免破坏 `ROADMAP.md` / 跨端引用。
5. **文件移动方式**：用 `git mv` 保留历史。
6. **迁移子树内部**相对链接默认不变，仅重写「逃逸」链接（§4.3-A 算法）。
7. **跨仓引用**：`deep-trace` / `DeepDiary` 仓库内对旧 `.../deep-dog/swrs/...` 绝对路径的引用**不在本仓库内**，本次仅更新本仓库规则文件中的示例；实际跨仓文档需另行通知同步（列入风险/跟进）。
8. 不改动任何业务代码逻辑，仅注释、文档、路径与规则文本。

---

## 6. 验证（Verification）

1. **死链检查**：迁移 + 重写后，对 `main/boards/deep-dog/docs/**` 与 `main/boards/deep-dog/**/*.md` 抽取所有相对 markdown 链接，逐一校验目标文件存在；输出 0 个 broken。
   - 建议一次性命令（不入库）：Python 脚本遍历 md，解析 `](...)`，`os.path.normpath` 后 `os.path.exists`，打印不存在的。
2. **残留扫描**：`grep -rn "swrs" main/boards/deep-dog .trae .cursor AGENTS.md scripts/deep_dog` 期望 **0 命中**（除 §5-7 提到的跨仓示例，若保留则需显式标注新路径）。
3. **结构核对**：`LS docs/` 应为 `README.md`、`CONVENTIONS.md`、`requirements/`、`interface/`、`ll/`；`requirements/` 下含迁移前 `swrs/` 的全部子项（`mqtt`、`vision`、`motor`、`input`、`net`、`config`、`dog`、`ws-mcp`、`architecture`、`ROADMAP.md`、`AUDIT_REPORT.md`）。
4. **文档自洽**：`docs/README.md` 场景路由表每一条都能落到真实目录；`docs/CONVENTIONS.md` 模板与既有 `LL-004`、`mqtt/modules/*` 的 structure 一致（抽查 2 篇比对）。
5. **构建不受影响**：`docs/` 下仅 `.md`/`.html`，不参与 `GLOB_RECURSE` 源码编译；**无需** `idf.py build` 验证（可选：确认 `idf.py build` 仍通过，证明无源码误伤）。
6. **规则生效**：新开对话触发需求/Bug 场景时，规则文件路径与 `docs/CONVENTIONS.md` 能正确引导落盘位置。

---

## 7. 风险与跟进

| 风险 | 说明 | 处置 |
|------|------|------|
| 链接重写遗漏 | 迁移树含 138+ 处跨层相对链接 | §6-1 全量死链检查兜底 |
| 跨仓引用断裂 | `deep-trace` / `DeepDiary` 内绝对路径仍指向 `.../deep-dog/swrs/` | 完成后单独通知两端同步；规则文件示例已更新 |
| 历史引用 | PR/issue/旧文档中的 `swrs` 文字 | 不追溯，仅保证仓库内文件零残留 |
| 编译误伤 | 迁移后 CMake 可能 GLOB 到新目录 | 仅 `.md`/`.html`，GLOB 只收 `.c/.cc/.cpp`，无影响 |
