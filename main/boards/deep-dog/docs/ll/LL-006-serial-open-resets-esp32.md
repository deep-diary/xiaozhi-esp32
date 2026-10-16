## LL-006 用 pyserial 打开 COM6 会触发 ESP32 自动复位，自动化测试会「测到刚重启的设备」

- **现象**：写自动化脚本读串口做设备发现（`pyserial.Serial('COM6', 115200)` → 读 20s → 关闭）后，
  紧接着跑 MQTT/HTTP 用例大面积超时或失败；串口日志时间戳每次都从 `I (249) …` 重新开始，
  说明设备在脚本每次「打开串口」时都被重启了。表现为 HTTP 8080 连接超时、MQTT 无指令应答。

- **根因**：ESP32 开发板的 USB 串口自动下载电路把 **DTR/RTS 接到 EN/GPIO0**。
  用 `pyserial` **打开**端口（以及关闭时）会翻转 DTR/RTS，等价于一次**硬复位脉冲**。
  于是「读日志」这个动作本身把设备重启了；后续用例其实打在一个正在启动的机器上。

- **正确做法**：
  1. 设备发现**优先用非侵入方式**——订阅 MQTT retain（`+/+/info/device`）反推 `device_id` 与 `ip`；
     仅在 MQTT 拿不到时才回退串口。
  2. 确需读串口时，明确它**会复位设备**：读完后等设备启动完成（约 15–20s，含 `post-activation`）再跑用例。
  3. 不要用「反复开关串口」来轮询日志；一次打开、持续读。
  4. 若只想看日志不想复位，需在电路/工具层面避免 DTR/RTS 翻转（或用 `idf.py monitor` 的既有复位语义）。

  ```python
  # 发现优先走 MQTT（不碰 DTR/RTS）
  client.subscribe("+/+/info/device", qos=0)   # retain 里含 device_id/ip
  # 串口仅作兜底，并接受「打开即复位」的代价
  ```

- **相关文件**：
  - `scripts/deep_dog/tests/dogtest/config.py`（`discover_from_serial` / `discover_from_mqtt`，auto 已改为 MQTT 优先）
  - `main/boards/deep-dog/docs/requirements/catalog/FINDINGS.md`（F-01 与本坑叠加的误导）
