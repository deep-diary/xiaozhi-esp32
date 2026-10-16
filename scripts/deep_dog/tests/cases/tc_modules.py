"""补充用例：track / touch / handle / can / motor / dp 的**无需额外硬件**命令类功能点。

与 `tc_mqtt.py` / `tc_http.py` 互补：这里专门覆盖「下发命令 → 观察 status 变化」的
可自动化项，且**尽量自恢复**（用完把设备状态改回去），避免影响其它用例。
"""

from __future__ import annotations

import json
import time

from dogtest.clients import envelope_data
from dogtest.registry import case

DEV = ("mqtt",)


def _b(ctx):
    return ctx.require_mqtt()


def _wait_status(ctx, bus, subject, predicate=None, timeout=10.0, since=None):
    topic = ctx.status(subject)

    def _pred(m):
        if m["topic"] != topic:
            return False
        try:
            d = envelope_data(json.loads(m["payload"]))
        except Exception:
            return False
        return predicate(d) if predicate else True

    msg = bus.wait_for(_pred, timeout=timeout, since=since or 0.0)
    return envelope_data(json.loads(msg["payload"])) if msg else None


def _cap(ctx, bus, name):
    bus.subscribe(ctx.info("device"))
    m = bus.wait_topic(ctx.info("device"), timeout=12)
    ctx.check(m is not None, "未收到 info/device")
    return envelope_data(json.loads(m["payload"])).get("capabilities", {}).get(name)


def _ext_pins(ctx, bus):
    bus.subscribe(ctx.info("device"))
    m = bus.wait_topic(ctx.info("device"), timeout=12)
    ctx.check(m is not None, "未收到 info/device")
    return envelope_data(json.loads(m["payload"])).get("ext_pins", {})


# ==========================================================================
# config · CFG-02 引出脚运行时模式（F0321 提交生效 / F0323 白名单 / F0324 待生效上报）
# ==========================================================================

@case("TC0321-config-extpin-commit", "F0321", "config", "ext_pins 启动提交生效（active）",
      "MQTT", "info/device.ext_pins.mode 有效且 ∈ supported_modes（提交成功、无 pending 残留）", needs=DEV)
def tc_extpin_commit(ctx):
    bus = _b(ctx)
    ep = _ext_pins(ctx, bus)
    mode = ep.get("mode")
    supported = ep.get("supported_modes") or []
    ctx.check(mode, f"ext_pins.mode 缺失：{ep}")
    ctx.check(mode in supported, f"生效模式 {mode} 不在 supported_modes {supported}")
    # apply_policy 固定 restart；pending_mode 在无待生效时应缺省或 null
    pending = ep.get("pending_mode")
    ctx.check(pending in (None, "", "null"), f"不应存在 pending_mode：{pending}")
    ctx.log(f"mode={mode} supported={len(supported)} pending={pending}")


@case("TC0323-config-extpin-supported-modes", "F0323", "config", "supported_modes 白名单",
      "MQTT", "白名单含本固件编入的全部模式（默认全模式编入）", needs=DEV)
def tc_extpin_supported(ctx):
    bus = _b(ctx)
    ep = _ext_pins(ctx, bus)
    supported = set(ep.get("supported_modes") or [])
    expect = {"can", "pwm", "led", "uart", "rs485", "io", "ad", "none"}
    missing = expect - supported
    ctx.check(not missing, f"白名单缺 {sorted(missing)}（实有 {sorted(supported)}）")
    ctx.log("supported=" + ",".join(sorted(supported)))


@case("TC0324-config-extpin-apply-policy", "F0324", "config", "apply_policy / pending 上报",
      "MQTT", "apply_policy == restart（重启生效语义固定）", needs=DEV)
def tc_extpin_policy(ctx):
    bus = _b(ctx)
    ep = _ext_pins(ctx, bus)
    ctx.check(ep.get("apply_policy") == "restart",
              f"apply_policy 应为 restart：{ep.get('apply_policy')}")
    ctx.log(f"apply_policy={ep.get('apply_policy')} gpio_a={ep.get('gpio_a')} gpio_b={ep.get('gpio_b')}")


# ==========================================================================
# device（F0008 数据点 / F0009 WiFi 状态 / F0010 无电量上报）
# ==========================================================================

def _schema_modules(ctx, bus):
    """取 info/schema 的 data.modules（形如 {device:{points:{...}}, imu:{...}}）。"""
    bus.subscribe(ctx.info("schema"))
    m = bus.wait_topic(ctx.info("schema"), timeout=12)
    ctx.check(m is not None, "未收到 info/schema")
    return (envelope_data(json.loads(m["payload"])).get("modules") or {})


@case("TC0008-device-datapoints", "F0008", "device", "device 数据点注册齐全",
      "MQTT", "info/schema.modules.device.points 含 10+ 个数据点", needs=DEV)
def tc_device_datapoints(ctx):
    bus = _b(ctx)
    mods = _schema_modules(ctx, bus)
    pts = ((mods.get("device") or {}).get("points") or {})
    want = ["device_id", "firmware", "board", "chip_model", "idf_version",
            "flash_size", "mac", "reset_reason", "online", "uptime_s", "free_heap", "ip", "rssi"]
    hit = [w for w in want if w in pts]
    ctx.check(len(hit) >= 10, f"device.points 不足：命中 {hit}（共 {len(pts)} 个点）")
    ctx.log(f"device.points={len(pts)} 命中 {len(hit)}/{len(want)}")


@case("TC0009-device-wifi-status", "F0009", "device", "WiFi SSID/信道仅入 status",
      "MQTT", "status/device 心跳含 wifi_ssid(str) 与 wifi_channel(int)", needs=DEV)
def tc_device_wifi(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("device"))

    def _ok(d):
        return isinstance(d, dict) and "wifi_ssid" in d and "wifi_channel" in d

    msg = bus.wait_for(lambda m: m["topic"] == ctx.status("device") and _ok(
        envelope_data(json.loads(m["payload"]))), timeout=16)
    if msg is None:
        ctx.check(False, "16s 内未收到含 wifi_ssid/wifi_channel 的心跳")
    d = envelope_data(json.loads(msg["payload"]))
    ctx.check(isinstance(d.get("wifi_ssid"), str), "wifi_ssid 应为字符串")
    ctx.check(isinstance(d.get("wifi_channel"), int), "wifi_channel 应为整数")
    ctx.log(f"ssid='{d.get('wifi_ssid')}' ch={d.get('wifi_channel')}")


@case("TC0010-device-power-unsupported", "F0010", "device", "无电量来源时不发伪数据",
      "MQTT", "info/device.power.supported == false", needs=DEV)
def tc_device_power(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.info("device"))
    m = bus.wait_topic(ctx.info("device"), timeout=12)
    ctx.check(m is not None, "未收到 info/device")
    power = envelope_data(json.loads(m["payload"])).get("power") or {}
    ctx.check(power.get("supported") is False,
              f"无电量硬件时 power.supported 应为 false：{power}")
    ctx.check("level_pct" not in power, "不应上报伪造的电量百分比")


# ==========================================================================
# imu（F0955 无下行 cmd / F0958 数据点）
# ==========================================================================

@case("TC0958-imu-datapoints", "F0958", "imu", "imu 数据点注册",
      "MQTT", "info/schema.modules.imu.points 含 accel/gyro/pitch/roll", needs=DEV)
def tc_imu_datapoints(ctx):
    bus = _b(ctx)
    mods = _schema_modules(ctx, bus)
    pts = ((mods.get("imu") or {}).get("points") or {})
    want = ["accel_x", "accel_g", "gyro_x", "pitch", "roll"]
    hit = [w for w in want if w in pts]
    ctx.check(len(hit) >= 3, f"imu.points 不足：命中 {hit}（共 {len(pts)} 个点）")
    ctx.log(f"imu.points={len(pts)} 命中 {len(hit)}/{len(want)}")


@case("TC0955-imu-no-cmd", "F0955", "imu", "imu 无下行 cmd（健壮性）",
      "MQTT", "向 cmd/imu 发消息/非 JSON 不导致异常，status/imu 续报", needs=DEV)
def tc_imu_no_cmd(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("imu"))
    bus.publish(ctx.cmd("imu"), {"whatever": 1, "ts": int(time.time())})
    bus.publish(ctx.cmd("imu"), "not-a-json")
    time.sleep(1.0)
    since = time.time()
    data = _wait_status(ctx, bus, "imu", timeout=8, since=since)
    ctx.check(data is not None, "发送 cmd/imu 后 status/imu 中断（可能崩溃）")


# ==========================================================================
# can（F1008 引脚上报）
# ==========================================================================

@case("TC1008-can-pins", "F1008", "can", "CAN 引脚上报（38/48）",
      "MQTT", "status/can.tx_gpio==38 且 rx_gpio==48", needs=DEV)
def tc_can_pins(ctx):
    bus = _b(ctx)
    if _cap(ctx, bus, "can") is not True:
        ctx.blocked("当前生效模式非 can（EXT_PIN != can）")
    bus.subscribe(ctx.status("can"))
    data = _wait_status(ctx, bus, "can", timeout=10)
    if data is None:
        ctx.blocked("CAN 已激活但未收到 status/can")
    ctx.check(data.get("tx_gpio") == 38, f"tx_gpio 应为 38：{data.get('tx_gpio')}")
    ctx.check(data.get("rx_gpio") == 48, f"rx_gpio 应为 48：{data.get('rx_gpio')}")


# ==========================================================================
# stream（F0402 mode=mjpeg 在 HTTP 关闭时应回 http_stream_disabled）
# ==========================================================================

@case("TC0402-stream-mode-mjpeg", "F0402", "stream", "cmd/stream mode=mjpeg 门控",
      "MQTT", "内置 HTTP 关闭时回 http_stream_disabled（不静默失败）", needs=DEV)
def tc_stream_mjpeg(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("stream"))
    time.sleep(1.0)
    since = time.time()
    bus.publish(ctx.cmd("stream"), {"action": "start", "mode": "stream", "ts": int(time.time())})
    data = _wait_status(ctx, bus, "stream", lambda d: bool(d.get("error")),
                        timeout=10, since=since)
    bus.publish(ctx.cmd("stream"), {"action": "stop", "ts": int(time.time())})   # 收尾
    if data is None:
        ctx.blocked("未见 error（可能内置 HTTP 已启用，mjpeg 被接受）")
    err = data.get("error")
    ctx.check(err == "http_stream_disabled",
              f"HTTP 关闭时 mode=stream 应回 http_stream_disabled，实为 {err}")
    ctx.log("error=" + str(err))


# ==========================================================================
# track（F0680 / F0682 / F0683）
# ==========================================================================

@case("TC0680-track-enable-disable", "F0680", "track", "cmd/track enable/disable",
      "MQTT", "enable → enabled=true；disable → enabled=false", needs=DEV)
def tc_track_enable(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("track"))
    for action, want in (("enable", True), ("disable", False)):
        since = time.time()
        bus.publish(ctx.cmd("track"), {"action": action, "ts": int(time.time())})
        data = _wait_status(ctx, bus, "track", lambda d: d.get("enabled") is want,
                            timeout=8, since=since)
        ctx.check(data is not None, f"cmd/track {action} 后 enabled 未变为 {want}")
        ctx.log(f"{action} → enabled={data.get('enabled')}")


@case("TC0682-track-no-face-following", "F0682", "track", "无人脸时 following=false",
      "MQTT", "使能跟踪但检测关/无目标 → following=false 且给出原因", needs=DEV)
def tc_track_following(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("track"))
    since = time.time()
    bus.publish(ctx.cmd("track"), {"action": "enable", "ts": int(time.time())})
    data = _wait_status(ctx, bus, "track", lambda d: d.get("enabled") is True,
                        timeout=8, since=since)
    ctx.check(data is not None, "enable 后未收到 status/track")
    ctx.check(data.get("following") is False,
              f"当前无可用人脸目标，following 应为 false：{data.get('following')}")
    bus.publish(ctx.cmd("track"), {"action": "disable", "ts": int(time.time())})   # 恢复


@case("TC0683-track-actuator-none", "F0683", "track", "actuator 恒为 none",
      "MQTT", "status/track.actuator == none（无真实 pan/tilt 执行器）", needs=DEV)
def tc_track_actuator(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("track"))
    since = time.time()
    bus.publish(ctx.cmd("track"), {"action": "disable", "ts": int(time.time())})
    data = _wait_status(ctx, bus, "track", timeout=8, since=since)
    ctx.check(data is not None, "未收到 status/track")
    ctx.check(data.get("actuator") == "none", f"actuator 应为 none：{data.get('actuator')}")


# ==========================================================================
# touch（F0704 / F0705 / F0706 / F0707 / F0708）
# ==========================================================================

@case("TC0704-touch-set-thresholds", "F0704", "touch", "cmd/touch set_thresholds",
      "MQTT", "改 press_abs_ratio 后 status/touch.thresholds 回显新值（随后还原）", needs=DEV)
def tc_touch_set_thresholds(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("touch"))
    cur = _wait_status(ctx, bus, "touch", timeout=10)
    ctx.check(cur is not None, "未收到 status/touch")
    base = float((cur.get("thresholds") or {}).get("press_abs_ratio", 0.25))
    target = round(base + 0.05, 4)
    since = time.time()
    bus.publish(ctx.cmd("touch"), {"action": "set_thresholds",
                                   "press_abs_ratio": target, "ts": int(time.time())})
    data = _wait_status(
        ctx, bus, "touch",
        lambda d: abs(float((d.get("thresholds") or {}).get("press_abs_ratio", -1)) - target) < 1e-3,
        timeout=8, since=since)
    ctx.check(data is not None, f"press_abs_ratio 未回显 {target}")
    ctx.log(f"press_abs_ratio {base} → {target}")
    # 还原
    bus.publish(ctx.cmd("touch"), {"action": "set_thresholds",
                                   "press_abs_ratio": base, "ts": int(time.time())})


@case("TC0705-touch-calibrate", "F0705", "touch", "cmd/touch calibrate",
      "MQTT", "calibrate 后 status/touch.calib.active=true（随后 cancel）", needs=DEV)
def tc_touch_calibrate(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("touch"))
    since = time.time()
    bus.publish(ctx.cmd("touch"), {"action": "calibrate", "button_id": 1, "samples": 20,
                                   "ts": int(time.time())})
    data = _wait_status(ctx, bus, "touch",
                        lambda d: bool((d.get("calib") or {}).get("active")),
                        timeout=10, since=since)
    ctx.check(data is not None, "calibrate 后 calib.active 未置位")
    ctx.log("calib=" + json.dumps(data.get("calib"), ensure_ascii=False)[:160])
    bus.publish(ctx.cmd("touch"), {"action": "cancel_calibrate", "ts": int(time.time())})


@case("TC0706-touch-reset-thresholds", "F0706", "touch", "cmd/touch reset_thresholds",
      "MQTT", "reset 后仍能收到 status/touch.thresholds（命令被接受）", needs=DEV)
def tc_touch_reset(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("touch"))
    since = time.time()
    bus.publish(ctx.cmd("touch"), {"action": "reset_thresholds", "ts": int(time.time())})
    data = _wait_status(ctx, bus, "touch", lambda d: "thresholds" in d, timeout=8, since=since)
    ctx.check(data is not None, "reset_thresholds 后未收到 thresholds")


@case("TC0707-touch-cancel-calibrate", "F0707", "touch", "cmd/touch cancel_calibrate",
      "MQTT", "cancel 后 calib.active=false", needs=DEV)
def tc_touch_cancel_calib(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("touch"))
    bus.publish(ctx.cmd("touch"), {"action": "calibrate", "button_id": 2, "samples": 20,
                                   "ts": int(time.time())})
    time.sleep(2)
    since = time.time()
    bus.publish(ctx.cmd("touch"), {"action": "cancel_calibrate", "ts": int(time.time())})
    data = _wait_status(ctx, bus, "touch",
                        lambda d: (d.get("calib") or {}).get("active") is False,
                        timeout=10, since=since)
    ctx.check(data is not None, "cancel_calibrate 后 calib.active 未复位")


@case("TC0708-touch-debug-flag", "F0708", "touch", "cmd/touch debug=true 带原始值",
      "MQTT", "debug=true 后 status/touch 带 debug[] 明细", needs=DEV)
def tc_touch_debug(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("touch"))
    since = time.time()
    bus.publish(ctx.cmd("touch"), {"action": "debug", "debug": True, "ts": int(time.time())})
    data = _wait_status(ctx, bus, "touch", lambda d: isinstance(d.get("debug"), list),
                        timeout=10, since=since)
    ctx.check(data is not None, "debug=true 后 status/touch 未带 debug[]")
    ctx.log(f"debug 项={len(data.get('debug') or [])}")


# ==========================================================================
# handle（F0759 / F0761 / F0763）
# ==========================================================================

@case("TC0759-handle-get-keymap", "F0759", "handle", "cmd/handle get_keymap",
      "MQTT", "get_keymap 后重新发布 status/handle/keymap", needs=DEV)
def tc_handle_get_keymap(ctx):
    bus = _b(ctx)
    topic = ctx.status("handle/keymap")
    bus.subscribe(topic)
    time.sleep(1.0)
    since = time.time()
    bus.publish(ctx.cmd("handle"), {"action": "get_keymap", "ts": int(time.time())})
    msg = bus.wait_topic(topic, timeout=8, since=since)
    ctx.check(msg is not None, "get_keymap 后未重发 status/handle/keymap")
    data = envelope_data(json.loads(msg["payload"]))
    ctx.check("bindings" in data or "axis_bindings" in data, "keymap 缺 bindings")


@case("TC0761-handle-set-profile", "F0761", "handle", "cmd/handle set_profile",
      "MQTT", "set_profile 被接受并重发 keymap（随后还原原 profile）", needs=DEV)
def tc_handle_profile(ctx):
    bus = _b(ctx)
    topic = ctx.status("handle/keymap")
    bus.subscribe(topic)
    time.sleep(1.0)
    snap = bus.wait_topic(topic, timeout=6)
    cur = "motor"
    if snap:
        try:
            cur = envelope_data(json.loads(snap["payload"])).get("profile") or "motor"
        except Exception:
            pass
    since = time.time()
    bus.publish(ctx.cmd("handle"), {"action": "set_profile", "profile": "off",
                                    "ts": int(time.time())})
    msg = bus.wait_topic(topic, timeout=8, since=since)
    ctx.check(msg is not None, "set_profile 后未重发 keymap")
    ctx.log(f"profile {cur} → off")
    bus.publish(ctx.cmd("handle"), {"action": "set_profile", "profile": cur,
                                    "ts": int(time.time())})   # 还原


@case("TC0763-handle-keymap-catalog", "F0763", "handle", "keymap catalog 按 profile 过滤",
      "MQTT", "status/handle/keymap.catalog 非空", needs=DEV)
def tc_handle_catalog(ctx):
    bus = _b(ctx)
    topic = ctx.status("handle/keymap")
    bus.subscribe(topic)
    msg = bus.wait_topic(topic, timeout=12)
    ctx.check(msg is not None, "未收到 status/handle/keymap")
    data = envelope_data(json.loads(msg["payload"]))
    cat = data.get("catalog")
    ctx.check(isinstance(cat, (list, dict)) and len(cat) > 0,
              f"catalog 为空或缺失：{type(cat)}")


# ==========================================================================
# can（F1001 / F1002）
# ==========================================================================

@case("TC1002-can-tunnel-toggle", "F1002", "can", "cmd/can tunnel 开关",
      "MQTT", "tunnel=true → status/can.tunnel=true；再关（还原）", needs=DEV)
def tc_can_tunnel(ctx):
    bus = _b(ctx)
    if _cap(ctx, bus, "can") is not True:
        ctx.blocked("当前生效模式非 can（EXT_PIN != can）")
    bus.subscribe(ctx.status("can"))
    for want in (True, False):
        since = time.time()
        bus.publish(ctx.cmd("can"), {"tunnel": want, "ts": int(time.time())})
        data = _wait_status(ctx, bus, "can", lambda d: d.get("tunnel") is want,
                            timeout=8, since=since)
        ctx.check(data is not None, f"tunnel 未变为 {want}")


@case("TC1001-can-tx-rejected", "F1001", "can", "cmd/can/tx 在 allow_tx=false 时被拒",
      "MQTT", "注入帧被拒绝且不影响总线状态（不崩溃、allow_tx 仍为 false）", needs=DEV)
def tc_can_tx_gate(ctx):
    bus = _b(ctx)
    if _cap(ctx, bus, "can") is not True:
        ctx.blocked("当前生效模式非 can（EXT_PIN != can）")
    bus.subscribe(ctx.status("can"))
    since = time.time()
    bus.publish(ctx.cmd("can/tx"), {"frames": [{"id": 0x123, "dlc": 8,
                                                "data": [1, 2, 3, 4, 5, 6, 7, 8]}],
                                    "ts": int(time.time())})
    time.sleep(1.5)
    # 只认**新鲜**帧（status/can 是 retain，直接读可能拿到上一会话的旧快照，见 FINDINGS F-14）
    data = _wait_status(ctx, bus, "can", timeout=8, since=since)
    ctx.check(data is not None, "注入后 status/can 无响应（可能崩溃）")
    ctx.check(data.get("allow_tx") is False, "allow_tx 应仍为 false（注入应被拒）")


# ==========================================================================
# motor（F1056 总线扫描）
# ==========================================================================

@case("TC1056-motor-scan-event", "F1056", "motor", "cmd/motor/scan → event/motor/scan",
      "MQTT", "扫描命令触发扫描事件（总线上无电机也应回事件）", needs=DEV)
def tc_motor_scan(ctx):
    bus = _b(ctx)
    if _cap(ctx, bus, "motor") is not True:
        ctx.blocked("电机栈未激活（EXT_PIN != can）")
    topic = ctx.topic("event", "motor/scan")
    bus.subscribe(topic)
    since = time.time()
    bus.publish(ctx.cmd("motor/scan"), {"ts": int(time.time())})
    msg = bus.wait_topic(topic, timeout=15, since=since)
    ctx.check(msg is not None, "15s 内未收到 event/motor/scan")
    ctx.log(msg["payload"][:160])


# ==========================================================================
# dp（F1252 / F1253 / F1272）
# ==========================================================================

@case("TC1252-dp-http-read", "F1252", "dp", "GET /api/datapoints/point/<id> 读值",
      "HTTP", "device.online 返回 200 且值与设备一致", needs=("http",))
def tc_dp_read(ctx):
    code, body = ctx.http.get_json("/api/datapoints/point/device.online")
    ctx.check(code == 200, f"HTTP {code}")
    ctx.check("true" in str(body).lower(), f"online 应为真：{str(body)[:120]}")


@case("TC1253-dp-http-write-validate", "F1253", "dp", "归一化写校验（类型/范围）",
      "HTTP", "非法类型/越界写入被拒（返回错误码而非 200 成功）", needs=("http",))
def tc_dp_write_validate(ctx):
    code, body = ctx.http.post_json("/api/datapoints/set",
                                    {"id": "device.uptime_s", "value": "not-an-int"})
    text = str(body).lower()
    ctx.check(("error" in text or "mismatch" in text or "range" in text or code >= 400),
              f"非法写入未被拒绝：{code} {text[:140]}")


@case("TC1272-telemetry-default-points", "F1272", "dp", "telemetry 默认点位",
      "MQTT", "status/telemetry.points 含 free_heap/rssi/uptime_s 等默认点", needs=DEV)
def tc_telemetry_points(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("telemetry"))
    data = _wait_status(ctx, bus, "telemetry", timeout=30)
    if data is None:
        ctx.skip("遥测未配置（telemetry 话题为空）→ 不发布")
    points = data.get("points") or {}
    if not isinstance(points, dict):
        ctx.check(False, f"points 形态异常：{type(points)}")
    hits = [k for k in points if any(s in k for s in ("free_heap", "rssi", "uptime"))]
    ctx.check(hits, f"默认点位缺失，实有：{sorted(points)[:10]}")
    ctx.log("points=" + ",".join(sorted(points)[:8]))


@case("TC1260a-dp-http-batch", "F1260", "dp", "POST /api/datapoints/batch 批量读",
      "HTTP", "200 且 data 同时含所求点位的 value/ts", needs=("http",))
def tc_dp_batch_read(ctx):
    code, body = ctx.http.post_json("/api/datapoints/batch",
                                    {"points": ["device.online", "device.device_id"]})
    ctx.check(code == 200, f"HTTP {code} body={str(body)[:140]}")
    data = body.get("data") if isinstance(body, dict) else None
    ctx.check(isinstance(data, dict), f"batch 响应缺 data：{str(body)[:160]}")
    for pid in ("device.online", "device.device_id"):
        ctx.check(pid in data, f"batch 返回缺 {pid}")
        ctx.check("value" in data[pid] and "ts" in data[pid], f"{pid} 项缺 value/ts")
    ctx.log("keys=" + ",".join(sorted(data)))
    # 未知点应 404
    code2, body2 = ctx.http.post_json("/api/datapoints/batch",
                                      {"points": ["device.not_exist"]})
    ctx.check(code2 == 404, f"未知点位应 404，实为 {code2}")


@case("TC1260b-dp-http-batch-set", "F1260", "dp", "POST /api/datapoints/batch_set 批量写",
      "HTTP", "合法项写入成功；未知点被拒并在 data.failed 列出", needs=("http",))
def tc_dp_batch_set(ctx):
    bus = _b(ctx)
    if _cap(ctx, bus, "can") is not True:
        ctx.blocked("当前生效模式非 can，无可用可写点（can.tunnel）")
    # 合法写入（can.tunnel 可写且无副作用），随后还原
    code, body = ctx.http.post_json("/api/datapoints/batch_set",
                                    {"points": [{"id": "can.tunnel", "value": True}]})
    ctx.check(code == 200, f"batch_set 合法写入失败：{code} {str(body)[:140]}")
    ctx.check(isinstance(body, dict) and body.get("ok") is True,
              f"batch_set ok 应为 true：{str(body)[:140]}")
    ctx.check((body.get("data") or {}).get("applied") == 1, "applied 应为 1")
    ctx.http.post_json("/api/datapoints/batch_set",
                       {"points": [{"id": "can.tunnel", "value": False}]})   # 还原
    # 非法项：未知点 → 失败明细
    code2, body2 = ctx.http.post_json("/api/datapoints/batch_set",
                                      {"points": [{"id": "device.not_exist", "value": 1}]})
    ctx.check(code2 >= 400, f"未知点批量写应报错，实为 {code2}")
    failed = ((body2 or {}).get("data") or {}).get("failed") or []
    ctx.check(any(f.get("id") == "device.not_exist" for f in failed),
              f"data.failed 未列出未知点：{str(body2)[:160]}")
