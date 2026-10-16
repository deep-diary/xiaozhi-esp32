# LL-009 裁任务栈必须用「稳态」stack_hwm，启动瞬间的值会严重低估

## 现象

同一固件、同一次开机，`dog_mem_rpt` 的 Top 任务表给出

```text
I (12960) dog_mem_rpt:   task mqtt_task        hwm=1360 domain=internal alloc=6144
```

看起来 `mqtt_task` 只用了 1360B / 6144B（余量 78%），像是可以大幅裁剪。
但同一时刻串口后一行才打印 `I (13460) dog_mqtt: board MQTT started (connected=1)` ——
即该 hwm 是**首连 TLS 之前**采的。60s 后走 WS MCP `self.board.diagnostics` 再取，同一任务：

```json
{"name": "mqtt_task", "stack_hwm": 4484, "stack_domain": "internal"}
```

余量从 78% 变成 **27%**。若按启动瞬间的 1360 裁到 3072，TLS/重连路径就会爆栈。

## 根因

`usStackHighWaterMark` 是**从任务创建至今的历史最低剩余**。任务刚起来（还没进入重连、TLS 握手、
大 JSON 解析、刷 Flash 等重路径）时该值接近「只跑过初始化」，据此判断余量必然偏乐观。

## 正确做法

1. 采样点必须选**稳态**：MQTT 已连（`board MQTT started (connected=1)`）、WS-MCP/VisionHub 已起，
   且至少跑过一次最重路径（如让设备处理一次 `self.board.diagnostics` 大 JSON）。
2. 驱动一轮重路径再用 `scripts/deep_dog/deep_dog_device_audit.py --device-ip <IP> --via none`
   取**全量** `tasks[]`（串口 `dog_mem_rpt` 默认只打 Top 8 条**最低** hwm，看不到大任务）。
3. 只在「hwm 余量 >25%」时才裁，且裁后仍留 >512B 余量（S09 MEM-03 验收线）。
4. face 相关任务（`face_boot` / `face_facedb` / `face_persist`）在 `DEEP_DOG_FACE_AI_DEFAULT_ENABLED=0`
   时**根本不常驻**，取不到 hwm —— 不得凭推测裁剪。

## 相关文件

- `main/boards/deep-dog/mqtt/memory_report.cc`（`LookupStackHint` / `kLogTopTasks=8`）
- `main/boards/deep-dog/docs/requirements/vision/server/S09-internal-sram-optimization.md`（MEM-03）
- `scripts/deep_dog/deep_dog_device_audit.py`（WS MCP diagnostics 取全量 tasks）
