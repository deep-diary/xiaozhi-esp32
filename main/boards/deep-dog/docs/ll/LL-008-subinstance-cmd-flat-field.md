# LL-008：多实例数据点写入 `<sub>_<point>` 与注册表 id `module.<sub>.<point>` 不一致（单实例用例会掩盖）

- **编号**：LL-008
- **日期**：2026-10-16
- **关键词**：子实例 / has_sub_instances / cmd 扁平字段 / 契约与实现漂移 / 单实例用例掩盖

## 现象

契约（`docs/interface/04-datapoint-registry.md` §3.7）与前端约定：`cmd/<module>` 写入的子实例字段名为
`<sub>_<point>`（如 `{"0_target": 45}`）。

但设备端 `HandleCmdMessage` 只做 `module_name + "." + field` 精确匹配：
`servo.0_target` **查不到**任何数据点（注册表里实际 id 是 `servo.0.target`），于是字段被**静默忽略** ——
写入「无报错、也不生效」。

单实例模块（如 `cmd/can` `{"tunnel": true}` → `can.tunnel`）能正常工作，
**端到端联调只覆盖了单实例**，因此该缺陷长期未被发现。

## 根因

扁平写入字段名（`<sub>_<point>`，用 `_` 分隔）与注册表内部点 id 命名（`module.<sub>.<point>`，用 `.` 分隔）
是两套编码；`HandleCmdMessage` 未做字段名 → 点 id 的**翻译**，只做了前缀拼接。
`FindPoint` 为全等匹配，无模糊/回退。

## 正确做法

1. `HandleCmdMessage` 解析字段时按模块类型分派：
   - 单实例：`<point>` → `module.<point>`；
   - 子实例（`entry.def.has_sub_instances`）：`<sub>_<point>` → `module.<sub>.<point>`，
     取**第一个** `_` 切分（点名自身可含 `_`，如 `min_angle` / `allow_tx`）。
2. 翻译**仅在 `has_sub_instances` 模块启用**，避免单实例点名含 `_` 时误切。
3. 未匹配字段继续**静默忽略**（兼容模块原有 `cmd/<module>` 业务字段）。
4. 新增多实例写入时，**必须单独验证多实例路径**：单测或真机至少要覆盖
   「`<sub>_<point>` → 命中 `module.<sub>.<point>` → 写回调拿到 sub_id ctx」三步，
   不能只用单实例用例回归。

## 相关文件

- 解析修复：`main/boards/deep-dog/mqtt/datapoint_registry.cc`（`HandleCmdMessage`）
- 多实例注册（id 形如 `servo.<sub>.target`）：同文件的 `RegisterMultiInstanceModule`
- 子实例写点样例：`main/boards/deep-dog/mqtt/modules/servo_datapoints.cc`
- 契约：`main/boards/deep-dog/docs/interface/04-datapoint-registry.md` §3.7
