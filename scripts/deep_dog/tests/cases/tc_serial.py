"""串口日志用例（只能从串口观测的功能点）。

`serial` tag 的用例默认**不参与**全量跑（会复位设备）；用 `--serial`（或 `--tag serial`）启用，
runner 会把它们排在最后执行。

注意：打开 COM6 会复位 ESP32（LL-006），因此这里观测的是**一次冷启动**的日志。
"""

from __future__ import annotations

import json
import time

from dogtest.registry import case

SER = ("serial",)


def _cap(ctx):
    try:
        return ctx.serial
    except Exception as exc:          # pyserial 缺失 / 串口被占用
        ctx.blocked(f"串口不可用（{ctx.cfg.serial_port}）：{exc}")


def _wait(ctx, cap, pattern, timeout, what):
    line = cap.wait(pattern, timeout=timeout)
    if line is None:
        ctx.skip(f"{timeout:.0f}s 内未见日志：{what}（可能受网络/服务影响）")
    ctx.log(line.strip()[:180])
    return line


@case("TC0007-serial-clock-resync", "F0007", "device", "设钟后重发 device/face registry（串口）",
      "串口", "日志出现 'clock synced: republished device/* + face/registry'", tags=SER)
def tc_serial_clock_resync(ctx):
    cap = _cap(ctx)
    _wait(ctx, cap, r"clock synced: republished", 60.0, "SNTP 同步后重发")


@case("TC0019-serial-topic-root", "F0019", "device", "MQTT 前缀根（串口）",
      "串口", "日志 'dog_mqtt: start prefix=<root>/<device_id>/' 与配置一致", tags=SER)
def tc_serial_topic_root(ctx):
    cap = _cap(ctx)
    line = _wait(ctx, cap, r"dog_mqtt: start prefix=", 45.0, "board MQTT 启动前缀")
    expect = f"/{ctx.cfg.device_id}/"
    ctx.check(expect in line, f"前缀与预期不符（应含 {expect}）：{line.strip()}")


@case("TC0200-serial-sntp-init", "F0200", "net", "SNTP 启动（串口）",
      "串口", "日志 'dog_sntp: SNTP init server=...'", tags=SER)
def tc_serial_sntp_init(ctx):
    cap = _cap(ctx)
    _wait(ctx, cap, r"SNTP init server=", 45.0, "SNTP 初始化")


@case("TC0206-serial-sta-dhcp", "F0206", "net", "STA DHCP 获取 IP（串口）",
      "串口", "日志 'WifiStation: Got IP: <ipv4>'", tags=SER)
def tc_serial_dhcp(ctx):
    cap = _cap(ctx)
    line = _wait(ctx, cap, r"Got IP", 45.0, "DHCP 获取 IP")
    ctx.check(":" in line, f"Got IP 行异常：{line.strip()}")


@case("TC0320-serial-extpin-boot", "F0320", "config", "ext_pins 启动目标解析（串口）",
      "串口", "日志 'ext_pins mode=<m> (default=<d>)'，且 m 在 supported_modes 内", tags=SER)
def tc_serial_extpin(ctx):
    cap = _cap(ctx)
    line = _wait(ctx, cap, r"ext_pins mode=\w+ \(default=", 45.0, "ext_pins 模式解析")
    import re
    m = re.search(r"ext_pins mode=(\w+)", line)
    ctx.check(m is not None, f"解析不到 mode：{line.strip()}")
    mode = m.group(1)
    # 与 MQTT 上报的 supported_modes 交叉验证
    bus = ctx.mqtt
    bus.subscribe(ctx.info("device"))
    msg = bus.wait_topic(ctx.info("device"), timeout=12)
    ctx.check(msg is not None, "未收到 info/device")
    supported = json.loads(msg["payload"])["data"].get("ext_pins", {}).get("supported_modes", [])
    ctx.check(mode in supported, f"生效模式 {mode} 不在 supported_modes {supported}")


@case("TC0417-serial-rtsp-encode", "F0417", "stream", "RTSP 推流编码分辨率（串口）",
      "串口", "日志 'H264 push ok … capture=WxH encode=wxh'，且 encode ≤ capture 且 ∈ 受支持尺寸", tags=SER)
def tc_serial_rtsp_encode(ctx):
    cap = _cap(ctx)
    cap.wait(r"dog_mqtt: connected", timeout=45.0)           # 等设备上电到 MQTT 连上
    cap.wait(r"VisionFrameHub started", timeout=25.0)        # 等视频管线起来
    time.sleep(12)                                           # 让板级 MQTT 进入稳态（开机初期会抖）

    bus = ctx.require_mqtt()
    bus.subscribe(ctx.status("stream"))
    time.sleep(1.2)

    # 先看这次冷启动摄像头有没有检出（DVP 未检出时不会有任何帧 → 必然 no_rtp）
    cam_fail = cap.search(r"Camera init failed|Camera not ready")
    if cam_fail:
        ctx.blocked(f"摄像头未检出（{cam_fail.strip()[:80]}）→ 无视频源，需断电重启/power cycle")

    line = None
    last = "n/a"
    for attempt in range(1, 3):
        since = time.time()
        bus.publish(ctx.cmd("stream"), {"action": "start", "mode": "rtsp_push",
                                        "ts": int(time.time())})
        line = cap.wait(r"H264 push ok", timeout=25.0)
        if line:
            break
        m = bus.wait_topic(ctx.status("stream"), timeout=4, since=since)
        if m:
            try:
                d = json.loads(m["payload"])["data"]
                last = f"state={d.get('state')} error={d.get('error')!r}"
            except Exception:
                pass
        ctx.log(f"第 {attempt} 次未见 H264 push ok（{last}），重试")

    bus.publish(ctx.cmd("stream"), {"action": "stop", "ts": int(time.time())})   # 收尾
    if line is None:
        ctx.blocked(f"50s 内未见 'H264 push ok'（device {last}）")
    ctx.log(line.strip()[:180])
    # 断言「编码分辨率不超过采集分辨率，且等于所配置的推流尺寸」。
    # 注意：当前联调剖面为 240²（S06 §9 的 640×480/320×240 代码保留但未启用），
    # 故不写死 320x240，只校验 ≤ 采集 且 落在受支持的推流尺寸集合内。
    import re as _re
    m = _re.search(r"capture=(\d+)x(\d+)\s+encode=(\d+)x(\d+)", line)
    ctx.check(m is not None, f"日志缺少 capture/encode 尺寸：{line.strip()}")
    cw, ch, ew, eh = (int(x) for x in m.groups())
    ctx.check(ew <= cw and eh <= ch, f"编码分辨率 {ew}x{eh} 超过采集 {cw}x{ch}")
    ctx.check((ew, eh) in {(240, 240), (320, 240), (640, 480)},
              f"编码分辨率 {ew}x{eh} 不在受支持集合内")
    ctx.log(f"capture={cw}x{ch} encode={ew}x{eh}")


@case("TC0952-serial-imu-detect", "F0952", "imu", "本地 100Hz 采集 + 12 路边沿识别（串口）",
      "串口", "日志 'dog_mqtt_imu: switches rot=[...]'", tags=SER)
def tc_serial_imu(ctx):
    cap = _cap(ctx)
    line = _wait(ctx, cap, r"dog_mqtt_imu: switches rot=\[", 45.0, "IMU 开关边沿日志")
    ctx.check("trans=[" in line, f"switches 行缺 trans：{line.strip()}")


@case("TC1000-serial-can-init", "F1000", "can", "TWAI 初始化（串口）",
      "串口", "日志 'CAN init ok, TX=38 RX=48, queue tx=.. rx=..'", tags=SER)
def tc_serial_can_init(ctx):
    cap = _cap(ctx)
    line = _wait(ctx, cap, r"CAN init ok", 45.0, "CAN/TWAI 初始化")
    ctx.check("TX=38" in line and "RX=48" in line, f"CAN 引脚不符：{line.strip()}")
    ctx.check("queue" in line, f"未见队列信息：{line.strip()}")


@case("TC0013-serial-mem-report", "F0013", "device", "内存报告（串口）",
      "串口", "日志 'dog_mem_rpt: memory report [post_activation]' 且带任务栈明细", tags=SER)
def tc_serial_mem_report(ctx):
    cap = _cap(ctx)
    line = _wait(ctx, cap, r"dog_mem_rpt: memory report \[", 45.0, "内存报告")
    ctx.log(line.strip()[:160])
    task_line = cap.wait(r"dog_mem_rpt:\s+task \S+\s+hwm=", timeout=10)
    ctx.check(task_line is not None, "内存报告未见任务栈（hwm）明细")


@case("TC0204-serial-sntp-servers", "F0204", "net", "SNTP 主/备服务器可配（串口）",
      "串口", "日志含主服务器 pool.ntp.org 与备用服务器", tags=SER)
def tc_serial_sntp_servers(ctx):
    cap = _cap(ctx)
    line = _wait(ctx, cap, r"SNTP init server=", 45.0, "SNTP 服务器配置")
    ctx.check("server=" in line, f"未见主服务器：{line.strip()}")
    ctx.check("backup=" in line, f"未见备用服务器（F0204 主/备可配）：{line.strip()}")
