# deep-dog 自动化测试框架

对 deep-dog 设备做**全盘功能回归**：MQTT / HTTP（数据点 REST）/ WS-MCP / RTSP 四通道。

- 用例编号：`TC<NNNN>-<domain>-<slug>`，注册表见
  [`docs/requirements/catalog/TESTS.md`](../../../main/boards/deep-dog/docs/requirements/catalog/TESTS.md)
- 功能点编号：`F<NNNN>-<domain>-<slug>`，见
  [`catalog/FEATURES.md`](../../../main/boards/deep-dog/docs/requirements/catalog/FEATURES.md)
- 结果报告：`docs/requirements/TEST_REPORT.md`（+ `.json`）

## 依赖

```bash
python -m pip install paho-mqtt websockets pyserial
```

## 运行

```bash
# 全量（自动发现设备：MQTT retain 优先，失败回退串口）
python scripts/deep_dog/tests/run.py

# 只跑某域 / 某用例
python scripts/deep_dog/tests/run.py --domain motor
python scripts/deep_dog/tests/run.py --case TC1071
python scripts/deep_dog/tests/run.py --list          # 只列用例

# 串口用例（tag=serial）：**会复位设备**，默认不跑；启用后排在最末执行
python scripts/deep_dog/tests/run.py --serial-cases
python scripts/deep_dog/tests/run.py --tag serial

# 显式指定连接参数（跳过发现）
python scripts/deep_dog/tests/run.py --discover none \
  --broker 10.99.79.54 --device-id 1051db8118fc --device-ip 10.99.79.59

# 凭据走环境变量（勿写入仓库）
export DEEP_DOG_MQTT_USER=...  DEEP_DOG_MQTT_PASS=...
```

退出码：`0` 全部 PASS；`1` 存在 FAIL/ERROR。

## 目录

```text
scripts/deep_dog/tests/
├── run.py                     # CLI 入口
├── dogtest/
│   ├── config.py              # 设备/连接配置 + 自动发现
│   ├── clients.py             # MqttBus / HttpClient / WsMcp
│   ├── serialcap.py           # 串口日志采集 + 断言（会复位设备，见 LL-006）
│   ├── registry.py            # @case 注册表
│   ├── report.py              # JSON / Markdown 报告
│   └── runner.py              # 执行引擎 + Ctx（断言/跳过/阻塞）
└── cases/
    ├── tc_mqtt.py             # 各模块 MQTT 用例
    ├── tc_modules.py          # 补充命令类用例（track/touch/handle/can/motor/dp）
    ├── tc_wsmcp.py            # WS-MCP 用例
    ├── tc_http.py             # 数据点 REST / 内置 HTTP 用例
    ├── tc_rtsp.py             # MediaMTX / RTSP 推流用例
    └── tc_serial.py           # 串口日志用例（tag=serial，需 --serial-cases）
```

## 用例写法

```python
from dogtest.registry import case

@case("TC1050-motor-enable", "F1050", "motor", "cmd/motor 使能/失能",
      "MQTT", "status/motor 更新", needs=("mqtt",))
def tc_motor_enable(ctx):
    bus = ctx.require_mqtt()
    bus.subscribe(ctx.status("motor"))
    since = time.time()
    bus.publish(ctx.cmd("motor"), {"enable": False, "ts": int(time.time())})
    data = _wait_status(ctx, bus, "motor", timeout=10, since=since)
    ctx.check(data is not None, "未收到 status/motor")
```

- 失败 → `raise AssertionError("现象")`
- 跳过（能力/剖面不适用）→ `ctx.skip("原因")`
- 阻塞（依赖不可用）→ `ctx.blocked("原因")`

## 注意

- **打开串口会复位 ESP32**：`pyserial` 打开 `COM6` 会触发 DTR/RTS 翻转 → 芯片自动复位。
  因此默认走 **MQTT retain 发现**（`--discover auto`），仅在 MQTT 发现不到时才回退串口。
- 设备当前存在 **internal SRAM 耗尽（P0）**，会导致板级 MQTT 掉线且无法重连、
  httpd 数分钟后不可达 —— 见
  [`catalog/FINDINGS.md`](../../../main/boards/deep-dog/docs/requirements/catalog/FINDINGS.md)。
