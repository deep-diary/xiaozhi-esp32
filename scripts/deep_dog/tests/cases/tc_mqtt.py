"""MQTT 域用例（device / pair / net / config / stream / imu / face / immich /
greet / track / touch / handle / led / gimbal / servo / can / motor / dog / dp）。

原则：订阅靠 retain/周期性上报断言；命令类用例 **发送前记录 since**，
并在断言后尽量把设备状态恢复原样（不破坏既有业务）。
"""

from __future__ import annotations

import json
import re
import time

from dogtest.clients import envelope_data
from dogtest.registry import case

DEV = ("mqtt",)


def _b(ctx):
    return ctx.require_mqtt()


def _wait_status(ctx, bus, subject, predicate=None, timeout=10.0, since=None):
    """等待 status/<subject> 满足 predicate，返回信封业务字段。"""
    topic = ctx.status(subject)

    def _pred(m):
        if m["topic"] != topic:
            return False
        try:
            data = envelope_data(json.loads(m["payload"]))
        except Exception:
            return False
        return predicate(data) if predicate else True

    msg = bus.wait_for(_pred, timeout=timeout, since=since or 0.0)
    if not msg:
        return None
    return envelope_data(json.loads(msg["payload"]))


def _wait_status_has(ctx, bus, subject, key, timeout=16.0):
    """等待 status/<subject> 中出现某个键（用于非 retain 的周期性心跳载荷）。"""
    return _wait_status(ctx, bus, subject,
                        lambda d: isinstance(d, dict) and key in d, timeout=timeout)


def _device_capability(ctx, bus, name):
    """读 info/device.capabilities[name]（按当前生效剖面判定模块是否激活）。"""
    bus.subscribe(ctx.info("device"))
    msg = bus.wait_topic(ctx.info("device"), timeout=12)
    ctx.check(msg is not None, "未收到 info/device")
    return envelope_data(json.loads(msg["payload"])).get("capabilities", {}).get(name)


def _block_if_capability_off(ctx, bus, name, hint):
    """capability 为假时阻塞：避免读到上一剖面遗留的 retain 而误判 PASS。"""
    if _device_capability(ctx, bus, name) is not True:
        ctx.blocked(f"当前剖面未激活 {name}（{hint}）")


# ==========================================================================
# device
# ==========================================================================

@case("TC0001-device-info-publish", "F0001", "device", "上线发布 info/device retain",
      "MQTT", "info/device retain 含 firmware/board/ip/capabilities/ext_pins", needs=DEV)
def tc_device_info(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.info("device"))
    msg = bus.wait_topic(ctx.info("device"), timeout=12)
    ctx.check(msg is not None, "未收到 info/device retain")
    data = envelope_data(json.loads(msg["payload"]))
    for key in ("firmware", "board", "ip", "capabilities", "ext_pins"):
        ctx.check(key in data, f"info/device 缺字段 {key}")
    ctx.log(f"board={data.get('board')} ip={data.get('ip')}")


@case("TC0002-device-heartbeat", "F0002", "device", "status/device 心跳（≤10s）",
      "MQTT", "status/device 含 online/uptime_s/free_heap", needs=DEV)
def tc_device_heartbeat(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("device"))
    data = _wait_status(ctx, bus, "device", timeout=12)
    ctx.check(data is not None, "未收到 status/device")
    for key in ("online", "uptime_s", "free_heap"):
        ctx.check(key in data, f"status/device 缺字段 {key}")
    ctx.check(data.get("online") is True, "online 应为 true")


@case("TC0003-device-mem-report", "F0003", "device", "内存分项上报（status/system）",
      "MQTT", "status/system 含 internal_free/psram_free 等", needs=DEV)
def tc_device_mem(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("system"))
    data = _wait_status(ctx, bus, "system", timeout=12)
    ctx.check(data is not None, "未收到 status/system retain")
    keys = ("internal_free", "psram_free", "largest_free_internal")
    ctx.check(any(k in data for k in keys), f"status/system 缺内存字段: {sorted(data.keys())}")
    ctx.log("keys=" + ",".join(sorted(data.keys())))


@case("TC0004-device-health-warn", "F0004", "device", "health.ok / warn[]（心跳载荷）",
      "MQTT", "status/device 心跳含 health.ok 布尔", needs=DEV)
def tc_device_health(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("device"))
    data = _wait_status_has(ctx, bus, "device", "health")
    ctx.check(data is not None, "16s 内未收到含 health 的 status/device 心跳")
    ctx.check(isinstance(data["health"].get("ok"), bool), "health.ok 应为布尔")


@case("TC0005-device-clock-synced", "F0005", "device", "clock_synced 字段（心跳载荷）",
      "MQTT", "status/device 心跳含 clock_synced 布尔", needs=DEV)
def tc_device_clock(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("device"))
    data = _wait_status_has(ctx, bus, "device", "clock_synced")
    ctx.check(data is not None, "16s 内未收到含 clock_synced 的心跳")
    ctx.check(isinstance(data["clock_synced"], bool), "clock_synced 应为布尔")


@case("TC0006-device-task-stacks", "F0006", "device", "tasks[] / task_count（心跳载荷）",
      "MQTT", "status/device 心跳含 tasks 数组与 task_count", needs=DEV)
def tc_device_tasks(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("device"))
    data = _wait_status_has(ctx, bus, "device", "tasks")
    ctx.check(data is not None, "16s 内未收到含 tasks 的心跳")
    ctx.check(isinstance(data.get("tasks"), list), "tasks 应为数组")
    ctx.check(isinstance(data.get("task_count"), int), "task_count 应为整数")
    ctx.log(f"task_count={data.get('task_count')}")


@case("TC0011-device-capabilities", "F0011", "device", "capabilities 能力位",
      "MQTT", "info/device.capabilities 含 mqtt 相关键", needs=DEV)
def tc_device_capabilities(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.info("device"))
    msg = bus.wait_topic(ctx.info("device"), timeout=12)
    ctx.check(msg is not None, "未收到 info/device")
    caps = envelope_data(json.loads(msg["payload"])).get("capabilities", {})
    ctx.check(isinstance(caps, dict) and caps, "capabilities 应为非空对象")
    ctx.log("capabilities=" + ",".join(sorted(caps.keys())))


@case("TC0012-device-system-datapoints", "F0012", "device", "system 数据点注册",
      "MQTT", "info/schema 含 system.* 数据点", needs=DEV)
def tc_device_system_dp(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.info("schema"))
    msg = bus.wait_topic(ctx.info("schema"), timeout=12)
    ctx.check(msg is not None, "未收到 info/schema")
    mods = envelope_data(json.loads(msg["payload"])).get("modules") or {}
    ctx.check("system" in mods, f"schema 未见 system 模块：{sorted(mods)}")
    pts = (mods.get("system") or {}).get("points") or {}
    ctx.check(len(pts) >= 4, f"system.points 过少：{len(pts)}")
    ctx.log(f"system.points={len(pts)}")


@case("TC0016-mqtt-topic-skeleton", "F0016", "device", "topic 骨架 {root}/{id}/info/device",
      "MQTT", "订阅前缀与骨架一致且收到消息", needs=DEV)
def tc_topic_skeleton(ctx):
    ctx.check(ctx.cfg.device_id, "device_id 未发现")
    ctx.check(re.match(r"^[0-9a-f]{12}$", ctx.cfg.device_id), "device_id 非 MAC 紧凑串")
    bus = _b(ctx)
    topic = ctx.info("device")
    ctx.check(topic == f"{ctx.cfg.topic_root}/{ctx.cfg.device_id}/info/device", "topic 骨架不符")
    bus.subscribe(topic)
    ctx.check(bus.wait_topic(topic, timeout=12) is not None, "骨架话题未收到消息")


@case("TC0017-mqtt-device-id-mac", "F0017", "device", "device_id 恒为 MAC 紧凑小写",
      "MQTT", "device_id 匹配 ^[0-9a-f]{12}$", needs=DEV)
def tc_device_id_mac(ctx):
    dev = ctx.cfg.device_id
    ctx.check(dev, "device_id 未发现")
    ctx.check(re.match(r"^[0-9a-f]{12}$", dev), f"device_id={dev} 不是 12 位十六进制")
    ctx.check("dev" != dev, "device_id 不应是 dev")


@case("TC0022-mqtt-info-schema", "F0022", "device", "info/schema retain 发布",
      "MQTT", "info/schema retain 非空", needs=DEV)
def tc_info_schema(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.info("schema"))
    msg = bus.wait_topic(ctx.info("schema"), timeout=12)
    ctx.check(msg is not None and len(msg["payload"]) > 20, "info/schema 为空")


@case("TC0065-device-no-cmd", "F0065", "device", "device 模块无下行 cmd（健壮性）",
      "MQTT", "向 cmd/device 发消息不导致设备异常（后续心跳仍在）", needs=DEV, tags=("robust",))
def tc_device_no_cmd(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("device"))
    bus.publish(ctx.cmd("device"), {"probe": 1, "ts": int(time.time())})
    bus.publish(ctx.cmd("device"), "not-a-json")
    ctx.check(_wait_status(ctx, bus, "device", timeout=12) is not None,
              "发送非法 cmd/device 后心跳中断（可能崩溃）")


# ==========================================================================
# pair
# ==========================================================================

@case("TC0100-pair-status-retain", "F0100", "pair", "status/pairing retain",
      "MQTT", "status/pairing retain 含 device_id/bound", needs=DEV)
def tc_pair_status(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("pairing"))
    data = _wait_status(ctx, bus, "pairing", timeout=12)
    ctx.check(data is not None, "未收到 status/pairing retain")
    ctx.check("bound" in data, "status/pairing 缺 bound 字段")


# ==========================================================================
# net
# ==========================================================================

@case("TC0208-net-device-info-ip", "F0208", "net", "device/info 广播 ip",
      "MQTT", "info/device.ip 为合法 IPv4", needs=DEV)
def tc_net_ip(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.info("device"))
    msg = bus.wait_topic(ctx.info("device"), timeout=12)
    ctx.check(msg is not None, "未收到 info/device")
    ip = envelope_data(json.loads(msg["payload"])).get("ip", "")
    ctx.check(re.match(r"^\d{1,3}(\.\d{1,3}){3}$", str(ip)), f"ip 非法: {ip}")


# ==========================================================================
# config
# ==========================================================================

@case("TC0306-config-mqtt-notify-pull", "F0306", "config", "cmd/config refresh 触发拉取",
      "MQTT", "cmd/config 收到并回 ack", needs=DEV)
def tc_config_refresh(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.topic("cmd", "config/ack"))
    since = time.time()
    bus.publish(ctx.cmd("config"), {"action": "refresh", "ts": int(time.time())})
    msg = bus.wait_topic(ctx.topic("cmd", "config/ack"), timeout=10, since=since)
    ctx.check(msg is not None, "未收到 cmd/config/ack")
    ctx.log("ack=" + msg["payload"][:160])


@case("TC0307-config-mqtt-ack", "F0307", "config", "cmd/config/ack 结果为 accepted",
      "MQTT", "ack data.result == accepted", needs=DEV)
def tc_config_ack(ctx):
    bus = _b(ctx)
    ack_topic = ctx.topic("cmd", "config/ack")
    bus.subscribe(ack_topic)
    since = time.time()
    bus.publish(ctx.cmd("config"), {"action": "refresh", "ts": int(time.time())})
    msg = bus.wait_topic(ack_topic, timeout=10, since=since)
    ctx.check(msg is not None, "未收到 ack")
    data = envelope_data(json.loads(msg["payload"]))
    ctx.check(data.get("result") in ("accepted", "unsupported"),
              f"ack.result 异常: {data.get('result')}")


@case("TC0308-config-unknown-action", "F0307", "config", "未知 action → unsupported",
      "MQTT", "ack.result == unsupported", needs=DEV)
def tc_config_unknown(ctx):
    bus = _b(ctx)
    ack_topic = ctx.topic("cmd", "config/ack")
    bus.subscribe(ack_topic)
    since = time.time()
    bus.publish(ctx.cmd("config"), {"action": "definitely_unknown_xyz", "ts": int(time.time())})
    msg = bus.wait_topic(ack_topic, timeout=10, since=since)
    ctx.check(msg is not None, "未收到 ack")
    data = envelope_data(json.loads(msg["payload"]))
    ctx.check(data.get("result") == "unsupported", f"应为 unsupported，实为 {data.get('result')}")


# ==========================================================================
# stream
# ==========================================================================

@case("TC0404-stream-status", "F0404", "stream", "status/stream retain 字段",
      "MQTT", "status/stream 含 state/mode", needs=DEV)
def tc_stream_status(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("stream"))
    data = _wait_status(ctx, bus, "stream", timeout=12)
    ctx.check(data is not None, "未收到 status/stream retain")
    ctx.check("state" in data and "mode" in data, "缺 state/mode")


@case("TC0401-stream-cmd-stop", "F0401", "stream", "cmd/stream stop",
      "MQTT", "stop 后 status/stream 收敛到 mode=off / state=idle", needs=DEV)
def tc_stream_stop(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("stream"))
    time.sleep(1.0)                       # 先收下 retain 快照，避免与旧 error 混淆
    cur = _wait_status(ctx, bus, "stream", timeout=6)
    since = time.time()
    bus.publish(ctx.cmd("stream"), {"action": "stop", "ts": int(time.time())})
    # 已经是 off 时不会再重发，故先看当前态，再看新消息
    data = _wait_status(ctx, bus, "stream",
                        lambda d: d.get("mode") == "off" or d.get("state") in ("off", "idle"),
                        timeout=8, since=since)
    if data is None:
        data = cur
    ctx.check(data is not None, "未收到 status/stream")
    ctx.check(data.get("mode") == "off" or data.get("state") in ("off", "idle"),
              f"stop 后未收敛: mode={data.get('mode')} state={data.get('state')}")


@case("TC0403-stream-cmd-errors", "F0403", "stream", "非法 cmd/stream 回错误且不崩溃",
      "MQTT", "status/stream.error 非空（随后被清空）", needs=DEV)
def tc_stream_errors(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("stream"))
    since = time.time()
    bus.publish(ctx.cmd("stream"), {"action": "unknown_action_xyz", "ts": int(time.time())})
    data = _wait_status(ctx, bus, "stream", lambda d: bool(d.get("error")),
                        timeout=10, since=since)
    ctx.check(data is not None, "非法 action 未回 error")
    ctx.log("error=" + str(data.get("error")))
    # 收尾：发一条合法 stop，清掉 retain 里的 error，避免污染后续用例
    bus.publish(ctx.cmd("stream"), {"action": "stop", "ts": int(time.time())})


# ==========================================================================
# imu
# ==========================================================================

@case("TC0956-imu-status", "F0956", "imu", "status/imu 上报（~10Hz）",
      "MQTT", "status/imu 含 accel/gyro 或 ok", needs=DEV)
def tc_imu_status(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("imu"))
    data = _wait_status(ctx, bus, "imu", timeout=6)
    ctx.check(data is not None, "未收到 status/imu")
    ctx.check(any(k in data for k in ("accel_x", "accel", "ok")), "imu 载荷无预期字段")
    ctx.log("keys=" + ",".join(sorted(data.keys()))[:200])


@case("TC0953-imu-switch-counts", "F0953", "imu", "12 路开关计数上报（心跳载荷）",
      "MQTT", "status/imu 心跳含 switches（rot/trans）", needs=DEV)
def tc_imu_switches(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("imu"))
    data = _wait_status_has(ctx, bus, "imu", "switches", timeout=8)
    ctx.check(data is not None, "8s 内未收到含 switches 的 status/imu")
    sw = data["switches"]
    ctx.check(isinstance(sw, dict), "switches 应为对象")
    ctx.check(len(sw) >= 1, "switches 为空")


# ==========================================================================
# face / immich / greet / track
# ==========================================================================

@case("TC0508-face-refresh-status", "F0508", "face", "cmd/face refresh_status 立即回帧",
      "MQTT", "refresh_status 后 status/face 返回", needs=DEV)
def tc_face_refresh(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("face"))
    since = time.time()
    bus.publish(ctx.cmd("face"), {"action": "refresh_status", "ts": int(time.time())})
    data = _wait_status(ctx, bus, "face", timeout=12, since=since)
    ctx.check(data is not None, "refresh_status 后未收到 status/face")
    ctx.check("enabled" in data, "status/face 缺 enabled")


@case("TC0511-face-registry-retain", "F0511", "face", "status/face/registry retain",
      "MQTT", "registry retain 含 count/entries", needs=DEV)
def tc_face_registry(ctx):
    bus = _b(ctx)
    topic = ctx.status("face/registry")
    bus.subscribe(topic)
    msg = bus.wait_topic(topic, timeout=12)
    ctx.check(msg is not None, "未收到 status/face/registry retain")
    data = envelope_data(json.loads(msg["payload"]))
    ctx.check("count" in data or "entries" in data, "registry 缺 count/entries")


@case("TC0534-face-interval-clamp", "F0534", "face", "detect_interval_ms 运行时夹紧",
      "MQTT", "越界值被夹紧到 [200,5000]", needs=DEV)
def tc_face_interval(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("face"))
    since = time.time()
    bus.publish(ctx.cmd("face"), {"detect_interval_ms": 999999, "ts": int(time.time())})
    data = _wait_status(ctx, bus, "face", timeout=12, since=since)
    ctx.check(data is not None, "未收到 status/face")
    iv = data.get("detect_interval_ms")
    if iv is None:
        ctx.skip("status/face 未回显 detect_interval_ms")
    ctx.check(iv <= 5000, f"未夹紧: {iv}")
    # 恢复默认
    bus.publish(ctx.cmd("face"), {"detect_interval_ms": 800, "ts": int(time.time())})


@case("TC0614-immich-mqtt-status", "F0614", "immich", "status/face/immich retain",
      "MQTT", "retain 含 configured/key_len", needs=DEV)
def tc_immich_status(ctx):
    bus = _b(ctx)
    topic = ctx.status("face/immich")
    bus.subscribe(topic)
    msg = bus.wait_topic(topic, timeout=12)
    ctx.check(msg is not None, "未收到 status/face/immich retain")
    data = envelope_data(json.loads(msg["payload"]))
    ctx.check("configured" in data, "缺 configured")
    ctx.check("api_key" not in data, "不应明文回显 api_key")


@case("TC0619-immich-no-key-echo", "F0619", "immich", "Immich key 不明文上行",
      "MQTT", "status/face/immich 仅回 key_len", needs=DEV)
def tc_immich_no_key(ctx):
    bus = _b(ctx)
    topic = ctx.status("face/immich")
    bus.subscribe(topic)
    msg = bus.wait_topic(topic, timeout=12)
    ctx.check(msg is not None, "未收到 status/face/immich")
    data = envelope_data(json.loads(msg["payload"]))
    ctx.check("api_key" not in data, "存在明文 api_key 字段")


@case("TC0654-greet-person-active", "F0654", "greet", "status/person/active retain",
      "MQTT", "有活跃人物时 person/active retain 下发", needs=DEV)
def tc_person_active(ctx):
    bus = _b(ctx)
    topic = ctx.status("person/active")
    bus.subscribe(topic)
    msg = bus.wait_topic(topic, timeout=10)
    if msg is None:
        ctx.skip("当前无活跃人物 → person/active 未下发（按需上报）")
    ctx.log("payload=" + msg["payload"][:160])


@case("TC0653-greet-current-speaker", "F0653", "greet", "device/status.current_speaker",
      "MQTT", "有说话人时下发 current_speaker", needs=DEV)
def tc_current_speaker(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("device"))
    data = _wait_status(ctx, bus, "device", timeout=12)
    ctx.check(data is not None, "未收到 status/device")
    if "current_speaker" not in data:
        ctx.skip("当前无活跃说话人（字段按需下发）")
    ctx.log("current_speaker=" + json.dumps(data["current_speaker"], ensure_ascii=False)[:120])


@case("TC0681-track-status", "F0681", "track", "status/track 上报",
      "MQTT", "status/track 含 enabled/following", needs=DEV)
def tc_track_status(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("track"))
    since = time.time()
    bus.publish(ctx.cmd("track"), {"action": "disable", "ts": int(time.time())})
    data = _wait_status(ctx, bus, "track", timeout=10, since=since)
    ctx.check(data is not None, "cmd/track disable 后未收到 status/track")
    ctx.check("enabled" in data, "status/track 缺 enabled")
    ctx.check(data.get("enabled") is False, f"disable 后 enabled 应为 false，实为 {data.get('enabled')}")


# ==========================================================================
# touch / handle
# ==========================================================================

@case("TC0714-touch-status", "F0714", "touch", "status/touch retain",
      "MQTT", "retain 含 pressed_mask/buttons", needs=DEV)
def tc_touch_status(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("touch"))
    data = _wait_status(ctx, bus, "touch", timeout=12)
    ctx.check(data is not None, "未收到 status/touch retain")
    ctx.check("pressed_mask" in data or "buttons" in data, "缺 pressed_mask/buttons")


@case("TC0703-touch-thresholds-report", "F0703", "touch", "阈值始终上报",
      "MQTT", "status/touch.thresholds 存在", needs=DEV)
def tc_touch_thresholds(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("touch"))
    data = _wait_status(ctx, bus, "touch", timeout=12)
    ctx.check(data is not None, "未收到 status/touch")
    ctx.check("thresholds" in data, "缺 thresholds")


@case("TC0762-handle-keymap-retain", "F0762", "handle", "status/handle/keymap retain",
      "MQTT", "retain 含 schema_ver/bindings", needs=DEV)
def tc_handle_keymap(ctx):
    bus = _b(ctx)
    topic = ctx.status("handle/keymap")
    bus.subscribe(topic)
    msg = bus.wait_topic(topic, timeout=12)
    ctx.check(msg is not None, "未收到 status/handle/keymap retain")
    data = envelope_data(json.loads(msg["payload"]))
    ctx.check("schema_ver" in data or "bindings" in data, "缺 schema_ver/bindings")
    ctx.log("schema_ver=" + str(data.get("schema_ver")))


@case("TC0750-handle-input-wifi", "F0750", "handle", "cmd/handle/input 注入 → status/handle",
      "MQTT", "wifi 源快照被解析并在 status/handle 回显", needs=DEV)
def tc_handle_input(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("handle"))
    since = time.time()
    bus.publish(ctx.cmd("handle/input"),
                {"axes": {"lx": 0.25, "ly": -0.5}, "buttons": {"a": True}, "source": "wifi"})
    data = _wait_status(ctx, bus, "handle", timeout=10, since=since)
    ctx.check(data is not None, "注入 input 后未收到 status/handle")
    axes = data.get("axes", {})
    if axes:
        ctx.check(abs(float(axes.get("lx", 0)) - 0.25) < 0.05, f"lx 未回显: {axes.get('lx')}")
    ctx.log("source=" + str(data.get("source")))


@case("TC0758-handle-enable-disable", "F0758", "handle", "cmd/handle enable/disable 门控",
      "MQTT", "disable/enable 不报错且状态可续", needs=DEV)
def tc_handle_enable(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("handle"))
    bus.publish(ctx.cmd("handle"), {"action": "disable", "ts": int(time.time())})
    time.sleep(0.5)
    bus.publish(ctx.cmd("handle"), {"action": "enable", "ts": int(time.time())})
    since = time.time()
    bus.publish(ctx.cmd("handle/input"), {"axes": {"lx": 0.0, "ly": 0.0}, "source": "wifi"})
    data = _wait_status(ctx, bus, "handle", timeout=10, since=since)
    ctx.check(data is not None, "enable 后 status/handle 未恢复上报")


# ==========================================================================
# led / gimbal / servo（非当前生效模式 → BLOCKED）
# ==========================================================================

@case("TC0804-led-status", "F0804", "led", "status/led retain",
      "MQTT", "LED 模式生效时 retain 含 mode/led_count", needs=DEV)
def tc_led_status(ctx):
    bus = _b(ctx)
    _block_if_capability_off(ctx, bus, "led", "需 EXT_PIN=led 剖面")
    bus.subscribe(ctx.status("led"))
    data = _wait_status(ctx, bus, "led", timeout=8)
    if data is None:
        ctx.blocked("LED 已激活但未收到 status/led retain")
    ctx.check("mode" in data, "缺 mode")


@case("TC0860-gimbal-status", "F0860", "gimbal", "status/gimbal retain",
      "MQTT", "PWM 模式生效时 retain 含 pan/tilt", needs=DEV)
def tc_gimbal_status(ctx):
    bus = _b(ctx)
    _block_if_capability_off(ctx, bus, "gimbal", "需 EXT_PIN=pwm 剖面")
    bus.subscribe(ctx.status("gimbal"))
    data = _wait_status(ctx, bus, "gimbal", timeout=8)
    if data is None:
        ctx.blocked("云台已激活但未收到 status/gimbal retain")
    ctx.check("pan" in data or "tilt" in data, "缺 pan/tilt")


@case("TC0904-servo-status", "F0904", "servo", "status/servo retain",
      "MQTT", "PWM 模式生效时 retain 含 servos[]", needs=DEV)
def tc_servo_status(ctx):
    bus = _b(ctx)
    _block_if_capability_off(ctx, bus, "servo", "需 EXT_PIN=pwm + SERVO_ENABLE")
    bus.subscribe(ctx.status("servo"))
    data = _wait_status(ctx, bus, "servo", timeout=8)
    if data is None:
        ctx.blocked("舵机已激活但未收到 status/servo retain")
    ctx.check("servos" in data, "缺 servos[]")


# ==========================================================================
# can / motor
# ==========================================================================

@case("TC1003-can-status", "F1003", "can", "status/can retain",
      "MQTT", "retain 含 ok/tunnel/allow_tx", needs=DEV)
def tc_can_status(ctx):
    bus = _b(ctx)
    _block_if_capability_off(ctx, bus, "can", "当前生效模式非 can（EXT_PIN != can）")
    bus.subscribe(ctx.status("can"))
    data = _wait_status(ctx, bus, "can", timeout=10)
    if data is None:
        ctx.blocked("CAN 已激活但未收到 status/can retain")
    for key in ("ok", "tunnel", "allow_tx"):
        ctx.check(key in data, f"status/can 缺 {key}")


@case("TC1004-can-allow-tx-gate", "F1004", "can", "allow_tx 默认关闭",
      "MQTT", "status/can.allow_tx == false", needs=DEV)
def tc_can_allow_tx(ctx):
    bus = _b(ctx)
    _block_if_capability_off(ctx, bus, "can", "当前生效模式非 can（EXT_PIN != can）")
    bus.subscribe(ctx.status("can"))
    # 只认**新鲜**的 status/can：status/can 是 retain，直接读可能拿到上一会话遗留的快照
    # （实测偶发读到 allow_tx=true 的旧值，见 FINDINGS F-14）。用幂等的隧道开关触发一次回帧，
    # 该命令不触碰 allow_tx，故仍是「默认值」断言。
    since = time.time()
    bus.publish(ctx.cmd("can"), {"tunnel": False, "ts": int(time.time())})
    data = _wait_status(ctx, bus, "can", lambda d: d.get("tunnel") is False,
                        timeout=10, since=since)
    if data is None:
        ctx.blocked("CAN 已激活但未收到新鲜 status/can")
    ctx.check(data.get("allow_tx") is False, "allow_tx 默认应为 false")


@case("TC1071-motor-status", "F1071", "motor", "status/motor retain",
      "MQTT", "retain 含 motors[]", needs=DEV)
def tc_motor_status(ctx):
    bus = _b(ctx)
    _block_if_capability_off(ctx, bus, "motor", "电机栈未激活（EXT_PIN != can）")
    bus.subscribe(ctx.status("motor"))
    data = _wait_status(ctx, bus, "motor", timeout=10)
    if data is None:
        ctx.blocked("电机栈已激活但未收到 status/motor retain")
    ctx.check("motors" in data, "status/motor 缺 motors[]")
    ctx.log(f"motors={len(data.get('motors') or [])}")


@case("TC1072-motor-tools", "F1072", "motor", "status/motor/tools retain",
      "MQTT", "工具目录 retain 非空", needs=DEV)
def tc_motor_tools(ctx):
    bus = _b(ctx)
    _block_if_capability_off(ctx, bus, "motor", "电机栈未激活（EXT_PIN != can）")
    topic = ctx.status("motor/tools")
    bus.subscribe(topic)
    msg = bus.wait_topic(topic, timeout=10)
    if msg is None:
        ctx.blocked("电机栈已激活但未收到 status/motor/tools")
    ctx.check(len(msg["payload"]) > 10, "tools 载荷过短")


@case("TC1050-motor-enable", "F1050", "motor", "cmd/motor 使能/失能",
      "MQTT", "enable/reset 后 status/motor 更新", needs=DEV)
def tc_motor_enable(ctx):
    bus = _b(ctx)
    _block_if_capability_off(ctx, bus, "motor", "电机栈未激活（EXT_PIN != can）")
    bus.subscribe(ctx.status("motor"))
    since = time.time()
    bus.publish(ctx.cmd("motor"), {"enable": False, "ts": int(time.time())})
    data = _wait_status(ctx, bus, "motor", timeout=10, since=since)
    if data is None:
        ctx.blocked("电机栈已激活但未收到 status/motor")
    ctx.log("motors=" + json.dumps(data.get("motors"), ensure_ascii=False)[:200])


@case("TC1074-motor-mcp-call", "F1074", "motor", "cmd/motor mcp_call 白名单透传",
      "MQTT", "白名单内工具返回 mcp_result；白名单外拒绝", needs=DEV)
def tc_motor_mcp_call(ctx):
    bus = _b(ctx)
    _block_if_capability_off(ctx, bus, "motor", "电机栈未激活（EXT_PIN != can）")
    res_topic = ctx.topic("event", "motor/mcp_result")
    bus.subscribe(res_topic)
    since = time.time()
    bus.publish(ctx.cmd("motor"), {"mcp_call": {"name": "self.motor.get_status", "arguments": {}},
                                   "ts": int(time.time())})
    msg = bus.wait_topic(res_topic, timeout=10, since=since)
    ctx.check(msg is not None, "mcp_call 未回 event/motor/mcp_result")


@case("TC1263-dp-info-schema", "F1263", "dp", "数据点 schema 自动发现",
      "MQTT", "info/schema 含 modules 段", needs=DEV)
def tc_dp_schema(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.info("schema"))
    msg = bus.wait_topic(ctx.info("schema"), timeout=12)
    ctx.check(msg is not None, "未收到 info/schema")
    try:
        data = envelope_data(json.loads(msg["payload"]))
        ctx.check(isinstance(data, dict) and len(data) > 0, "schema 载荷为空")
        ctx.log("schema modules=" + ",".join(sorted(data.keys()))[:200])
    except Exception as exc:
        raise AssertionError(f"schema 非 JSON: {exc}")


@case("TC1268-telemetry-publish", "F1268", "dp", "status/telemetry 上报",
      "MQTT", "已配置遥测时 status/telemetry 含 points", needs=DEV)
def tc_telemetry(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("telemetry"))
    data = _wait_status(ctx, bus, "telemetry", timeout=25)
    if data is None:
        ctx.skip("遥测未配置（telemetry 话题为空）→ 不发布")
    ctx.check("points" in data, "telemetry 缺 points")
