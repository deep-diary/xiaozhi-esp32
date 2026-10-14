---
description: 新功能开发前必须先确认/补齐 main/boards/deep-dog/swrs 需求文档，禁止无出处编码
alwaysApply: true
---

# deep-dog：需求文档先行（SWRS First）

用户提出新功能/改动需求并要求开发时，**禁止直接改代码**。必须先完成需求出处确认，再实现。

## 权威目录

`main/boards/deep-dog/swrs/`（索引见 `swrs/README.md`，顺序见 `swrs/ROADMAP.md`）

子域参考：`mqtt/` · `dog/` · `vision/` · `input/`

## 强制流程（每次新需求）

1. **检索匹配文档**  
   在 `swrs/` 下按关键词、模块 ID、ROADMAP 序号查找是否已有对应需求。
2. **无匹配 → 先文档后代码**  
   - 规划存放路径（沿用现有子域与编号风格，如 `mqtt/modules/NN-name.md`、`vision/server/SNN-*.md`、`input/INN-*.md`）。  
   - **新建或更新**需求文档（目标、边界、验收、依赖、与 ROADMAP/相关 README 索引的挂接）。  
   - 涉及 MQTT 契约时同步更新 `mqtt/protocol/deep-dog-mqtt.yml` 与相关 modules。  
   - 文档就绪后，再开始写/改实现代码。
3. **有匹配 → 对齐后再开发**  
   若现有文档与本次输入不一致，先更新文档，再按文档实现。
4. **出处可追溯**  
   动手改代码前，在回复中明确写出本次功能对应的需求文档路径（可多项）。每个功能点都必须能指回至少一份 `swrs/` 文档。

## 禁止

- 未确认/未补齐 SWRS 就直接实现  
- 只改代码不落需求（含「先写代码回头补文档」）  
- 扩大改动面到未写入 ROADMAP / SWRS 的范围（见 `swrs/README.md` 约定）

## 自检

- [ ] 已检索 `swrs/`，确认匹配或已规划路径  
- [ ] 新建/更新的需求文档与索引（必要时 ROADMAP）已就绪  
- [ ] 回复中已列出本次实现的需求出处路径  
- [ ] 才开始改实现代码
