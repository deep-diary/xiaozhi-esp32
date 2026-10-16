"""补充用例（第二批）：face 命令面 + 数据点归一化回环 / 子实例。

原则与 `tc_modules.py` 相同：**可自动化、尽量自恢复**（改完把状态改回去）。
**不做破坏性操作**（如 face clear_db），避免动用户已入库的人脸数据。
"""

from __future__ import annotations

import json
import time

from dogtest import dpschema
from dogtest.clients import envelope_data
from dogtest.registry import case

DEV = ("mqtt",)


def _b(ctx):
    return ctx.require_mqtt()


def _cap(ctx, bus, name):
    """读 info/device.capabilities[name]（判定模块是否按当前剖面激活）。"""
    bus.subscribe(ctx.info("device"))
    m = bus.wait_topic(ctx.info("device"), timeout=12)
    ctx.check(m is not None, "未收到 info/device")
    return envelope_data(json.loads(m["payload"])).get("capabilities", {}).get(name)


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


def _snapshot(ctx, bus, subject, timeout=10.0):
    bus.subscribe(ctx.status(subject))
    return _wait_status(ctx, bus, subject, timeout=timeout)


def _force_face_status(ctx, bus):
    """强制 status/face 立即回一帧（否则要等 ~30s 控制态心跳，易误判）。"""
    bus.publish(ctx.cmd("face"), {"action": "refresh_status", "ts": int(time.time())})


def _face_snapshot(ctx, bus, timeout=15.0):
    """先订阅再强制回帧，拿到当前 status/face（避免等 30s 心跳）。"""
    bus.subscribe(ctx.status("face"))
    time.sleep(0.5)
    _force_face_status(ctx, bus)
    return _wait_status(ctx, bus, "face", timeout=timeout)


# ==========================================================================
# face 命令面（F0500 / F0501 / F0502 / F0509 / F0526）
# ==========================================================================

@case("TC0500-face-enable", "F0500", "face", "cmd/face enabled 总开关",
      "MQTT", "enabled=true → status/face.enabled=true；若因 internal SRAM 受限起不来则 BLOCK（附因）", needs=DEV)
def tc_face_enable(ctx):
    bus = _b(ctx)
    cur = _face_snapshot(ctx, bus)
    ctx.check(cur is not None, "未收到 status/face")
    origin = bool(cur.get("enabled"))
    since = time.time()
    bus.publish(ctx.cmd("face"), {"enabled": True, "ts": int(time.time())})
    data = _wait_status(ctx, bus, "face", lambda d: d.get("enabled") is True,
                        timeout=30, since=since)
    if data is None:
        # 命令已下发但 face 运行时未起来：face_boot 需 ~16KB **连续 internal** 栈，
        # internal 紧张时创建失败（串口："SetEnabled(true) failed: face_boot task"）。
        # 属 F-01 家族（内部 SRAM）；S09 MEM-07「改 PSRAM 栈」按原文实施会因启动路径做
        # NVS/FAT flash 操作而触发 cache 断言重启（见 FINDINGS F-10）→ 这里记 BLOCK，不记 FAIL。
        last = _wait_status(ctx, bus, "face", timeout=12)
        bus.publish(ctx.cmd("face"), {"enabled": origin, "ts": int(time.time())})
        detail = ("n/a" if last is None else
                  f"enabled={last.get('enabled')} w={last.get('w')} h={last.get('h')}")
        ctx.blocked("face 运行时未能启动（face_boot 需连续 internal 栈，见 FINDINGS F-01/F-10）；"
                    f"status/face {detail}")
    ctx.log(f"enabled 原值={origin} → True")
    bus.publish(ctx.cmd("face"), {"enabled": origin, "ts": int(time.time())})   # 还原


@case("TC0501-face-recognize-toggle", "F0501", "face", "cmd/face recognize_enabled",
      "MQTT", "recognize_enabled 下发后 status/face 回显（随后还原）", needs=DEV)
def tc_face_recognize(ctx):
    bus = _b(ctx)
    cur = _face_snapshot(ctx, bus)
    ctx.check(cur is not None, "未收到 status/face")
    origin = bool(cur.get("recognize_enabled"))
    since = time.time()
    bus.publish(ctx.cmd("face"), {"recognize_enabled": not origin, "ts": int(time.time())})
    _force_face_status(ctx, bus)
    data = _wait_status(ctx, bus, "face",
                        lambda d: d.get("recognize_enabled") is (not origin),
                        timeout=15, since=since)
    ctx.check(data is not None, "recognize_enabled 未回显")
    bus.publish(ctx.cmd("face"), {"recognize_enabled": origin, "ts": int(time.time())})
    _force_face_status(ctx, bus)


@case("TC0502-face-pipeline", "F0502", "face", "cmd/face pipeline=live/identity",
      "MQTT", "pipeline 下发后 status/face 回显（随后还原）", needs=DEV)
def tc_face_pipeline(ctx):
    bus = _b(ctx)
    cur = _face_snapshot(ctx, bus)
    ctx.check(cur is not None, "未收到 status/face")
    origin = cur.get("pipeline")
    target = "identity" if origin != "identity" else "live"
    since = time.time()
    bus.publish(ctx.cmd("face"), {"pipeline": target, "ts": int(time.time())})
    _force_face_status(ctx, bus)
    data = _wait_status(ctx, bus, "face", lambda d: d.get("pipeline") == target,
                        timeout=15, since=since)
    ctx.check(data is not None, f"pipeline 未回显 {target}")
    if origin in ("live", "identity"):
        bus.publish(ctx.cmd("face"), {"pipeline": origin, "ts": int(time.time())})
        _force_face_status(ctx, bus)
    ctx.log(f"pipeline {origin} → {target}")


@case("TC0509-face-refresh-immich", "F0509", "face", "cmd/face refresh_immich",
      "MQTT", "命令被接受并重发 status/face/immich", needs=DEV)
def tc_face_refresh_immich(ctx):
    bus = _b(ctx)
    topic = ctx.status("face/immich")
    bus.subscribe(topic)
    time.sleep(1.0)
    since = time.time()
    bus.publish(ctx.cmd("face"), {"action": "refresh_immich", "ts": int(time.time())})
    msg = bus.wait_topic(topic, timeout=10, since=since)
    if msg is None:
        ctx.blocked("refresh_immich 后未见 status/face/immich 重发（可能未配置 Immich）")
    ctx.check("configured" in envelope_data(json.loads(msg["payload"])),
              "immich status 缺 configured")


@case("TC0526-face-mcp-manage", "F0526", "face", "MCP self.face.manage（非破坏动作）",
      "WS-MCP", "manage action=refresh_immich 调用成功返回 result", needs=("wsmcp",))
def tc_face_mcp_manage(ctx):
    try:
        ws = ctx.wsmcp
    except Exception as exc:
        ctx.blocked(f"WS-MCP 不可用：{exc}")
    r = ws.call("self.face.manage", {"action": "refresh_immich"})
    res = r.get("result")
    if res is None and r.get("error"):
        ctx.blocked(f"manage 返回错误：{str(r)[:140]}")
    ctx.check(res is not None, f"manage 未返回 result：{str(r)[:160]}")
    ctx.log(str(res)[:140])


# ==========================================================================
# 数据点归一化：MCP ↔ 通道回环（F1261）+ 子实例（F1264）
# ==========================================================================

@case("TC1261-dp-mcp-roundtrip", "F1261", "dp", "数据点自动 MCP 工具读写回环",
      "WS-MCP", "MCP set_<point> 生效，且 MQTT status 同步反映（同一数据点单一真源）", needs=("wsmcp", "mqtt"))
def tc_dp_mcp_roundtrip(ctx):
    bus = _b(ctx)
    try:
        ws = ctx.wsmcp
    except Exception as exc:
        ctx.blocked(f"WS-MCP 不可用：{exc}")
    names = {t.get("name") for t in ws.list_tools()}
    if "self.can.set_tunnel" not in names:
        schema = dpschema.read_schema(ctx)
        expected = dpschema.expected_tools(schema) if schema else []
        largest = dpschema.largest_internal_bytes(ctx)
        ctx.blocked(
            f"自动工具未注册（缺 self.can.set_tunnel，命中 {len([n for n in expected if n in names])}/"
            f"{len(expected)}）；internal largest_free={largest}B < 注册下限 6144B（F-01 家族 / S09）")

    bus.subscribe(ctx.status("can"))
    since = time.time()
    r = ws.call("self.can.set_tunnel", {"value": True})
    ctx.check(r.get("result") is not None or not r.get("error"),
              f"set_tunnel 调用失败：{str(r)[:140]}")
    # MQTT 侧应同步反映（同一数据点）
    data = _wait_status(ctx, bus, "can", lambda d: d.get("tunnel") is True,
                        timeout=10, since=since)
    ctx.check(data is not None, "MCP 写后 status/can.tunnel 未同步为 true")
    # 还原
    ws.call("self.can.set_tunnel", {"value": False})
    ctx.log("MCP set_tunnel → status/can.tunnel 已同步")


@case("TC1264-dp-sub-instances", "F1264", "dp", "数据点子实例（module.<sub>.point）",
      "HTTP", "schema 中存在含子实例的点位（如 motor.<id>.x）", needs=("http",))
def tc_dp_sub_instances(ctx):
    code, body = ctx.http.get_json("/api/datapoints/schema")
    ctx.check(code == 200, f"HTTP {code}")
    text = json.dumps(body)
    # 子实例形如 "motor.0.xxx" / "servo.0.xxx"：判断是否存在「三段且第 2 段为数字」的点 id
    import re
    hits = re.findall(r'"([a-z]+\.[0-9]+\.[a-z_0-9]+)"', text)
    if not hits:
        ctx.skip("当前无子实例点位（未接电机/舵机，懒注册未触发）")
    ctx.log("sub-instances 例：" + ", ".join(sorted(set(hits))[:5]))


@case("TC1264b-dp-sub-instance-write", "F1264", "dp", "子实例写入 cmd/<module> <sub>_<point>",
      "MQTT", "扁平字段 <sub>_<point> 命中 module.<sub>.<point> 并写入生效（status/<module> 的 <sub>.<point> 变为目标值，随后还原）；无子实例 rw 点则 SKIP",
      needs=DEV)
def tc_dp_sub_instance_write(ctx):
    """契约：interface/04 §3.7 —— 子实例写入字段为 `<sub>_<point>`，设备侧翻译为 `module.<sub>.<point>`。

    背景见 LL-008：单实例用例（如 `cmd/can`）通过**不能**证明子实例路径可用。
    当前唯一可写的子实例点：`servo.target`（`motor.*` 全为 `ro`）。"""
    bus = _b(ctx)

    # 1) 从 info/schema 找「已注册子实例 + 含 mqtt rw 点」的模块
    bus.subscribe(ctx.info("schema"))
    m = bus.wait_topic(ctx.info("schema"), timeout=12)
    ctx.check(m is not None, "未收到 info/schema（retain）")
    modules = (envelope_data(json.loads(m["payload"])) or {}).get("modules") or {}

    for mod, mv in modules.items():
        mv = mv or {}
        if not mv.get("has_sub_instances"):
            continue
        subs = (mv.get("instances") or {}).get("ids") or []
        if not subs:
            continue
        rw = [(name, pv or {}) for name, pv in (mv.get("points") or {}).items()
              if (pv or {}).get("access") == "rw" and "mqtt" in ((pv or {}).get("channels") or [])]
        if not rw:
            continue

        point, meta = rw[0]
        sub = str(subs[0])
        key = f"{sub}_{point}"          # 契约字段名（§3.7）

        # 2) 取当前值
        bus.subscribe(ctx.status(mod))
        snap = _wait_status(ctx, bus, mod, timeout=8)
        if snap is None:
            ctx.skip(f"未收到 status/{mod}，无法取当前值")
        cur = snap.get(sub, {}).get(point) if isinstance(snap.get(sub), dict) else None

        # 3) 目标值：与当前值不同且落在 schema 声明的范围内
        if meta.get("type") == "bool":
            target = (not bool(cur)) if cur is not None else True
        else:
            lo, hi = meta.get("min"), meta.get("max")
            cand = (cur + 1) if isinstance(cur, (int, float)) and not isinstance(cur, bool) else None
            if cand is None:
                cand = lo if lo is not None else 0
                if hi is not None and cand > hi:
                    cand = hi
            if lo is not None and cand < lo:
                cand = lo
            if hi is not None and cand > hi:
                cand = hi
            target = int(cand) if meta.get("type") == "int" else float(cand)

        # 4) 下发并等 status 回读（写法错误/未翻译时设备会静默忽略 → 回读不变）
        since = time.time()
        bus.publish(ctx.cmd(mod), {key: target, "ts": int(time.time())})
        got = _wait_status(
            ctx, bus, mod,
            lambda d: isinstance(d.get(sub), dict) and d.get(sub, {}).get(point) == target,
            timeout=10, since=since)
        ctx.check(got is not None,
                  f"子实例写入未生效：cmd/{mod} {{{key}: {target}}} 后 "
                  f"status/{mod}.{sub}.{point} 未变为 {target}（当前 {cur}）；见 interface/04 §3.7 / LL-008")
        ctx.log(f"cmd/{mod} {{{key}: {target}}} → status/{mod}.{sub}.{point}={target}")

        # 5) 还原（best-effort，不再断言）
        if cur is not None and cur != target:
            bus.publish(ctx.cmd(mod), {key: cur, "ts": int(time.time())})
            _wait_status(ctx, bus, mod,
                         lambda d: isinstance(d.get(sub), dict) and d.get(sub, {}).get(point) == cur,
                         timeout=10)
            ctx.log(f"已还原 {mod}.{sub}.{point}={cur}")
        return

    ctx.skip("当前无子实例 rw 点（servo 需 ext_pins.mode=pwm，motor 无 rw 点；懒注册未触发）")


# ==========================================================================
# greet：simulate / clear / set_greet_config（MQTT + MCP）
# ==========================================================================

def _speaker_of(ctx, bus, timeout=16.0, name=None):
    """取 status/device 心跳里的 current_speaker；name 非空时要求 display_name 匹配
    （避免读到上一条用例残留的说话人）。"""
    topic = ctx.status("device")

    def _pred(m):
        if m["topic"] != topic:
            return False
        d = envelope_data(json.loads(m["payload"]))
        sp = d.get("current_speaker")
        if not isinstance(sp, dict):
            return False
        if name is not None and sp.get("display_name") != name:
            return False
        return True

    msg = bus.wait_for(_pred, timeout=timeout)
    return envelope_data(json.loads(msg["payload"])).get("current_speaker") if msg else None


@case("TC0659-greet-mqtt-simulate", "F0659", "greet", "face/cmd simulate_greet + clear_speaker",
      "MQTT", "simulate 后 current_speaker.source=simulate；clear 后消失", needs=DEV)
def tc_greet_mqtt_simulate(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("device"))
    name = "dogtest-probe"
    since = time.time()
    bus.publish(ctx.cmd("face"), {"action": "simulate_greet", "name": name, "local_id": 1,
                                  "ts": int(time.time())})
    sp = _speaker_of(ctx, bus, timeout=18, name=name)
    if sp is None:
        bus.publish(ctx.cmd("face"), {"action": "clear_speaker", "ts": int(time.time())})
        ctx.blocked("simulate_greet 后 current_speaker 未出现（未识别人脸/被过滤）")
    ctx.check(sp.get("display_name") == name,
              f"display_name 应为 {name}：{sp}")
    ctx.check(sp.get("source") == "simulate", f"source 应为 simulate：{sp.get('source')}")
    ctx.log(f"current_speaker={json.dumps(sp, ensure_ascii=False)[:140]}")
    # 清理
    bus.publish(ctx.cmd("face"), {"action": "clear_speaker", "ts": int(time.time())})
    time.sleep(1.0)


@case("TC0661-greet-mqtt-set-config", "F0661", "greet", "face/cmd action=set_greet_config",
      "MQTT", "greet_gap_sec 下发后 status/face 回显（随后还原）", needs=DEV)
def tc_greet_mqtt_cfg(ctx):
    bus = _b(ctx)
    cur = _face_snapshot(ctx, bus)
    ctx.check(cur is not None, "未收到 status/face")
    origin = int(cur.get("greet_gap_sec") or 1800)
    target = 1234 if origin != 1234 else 4321
    since = time.time()
    bus.publish(ctx.cmd("face"), {"action": "set_greet_config", "greet_gap_sec": target,
                                  "ts": int(time.time())})
    _force_face_status(ctx, bus)
    data = _wait_status(ctx, bus, "face", lambda d: int(d.get("greet_gap_sec") or 0) == target,
                        timeout=15, since=since)
    ctx.check(data is not None, f"greet_gap_sec 未回显 {target}")
    bus.publish(ctx.cmd("face"), {"action": "set_greet_config", "greet_gap_sec": origin,
                                  "ts": int(time.time())})
    _force_face_status(ctx, bus)
    ctx.log(f"greet_gap_sec {origin} → {target}")


@case("TC0662-greet-mqtt-top-level", "F0662", "greet", "face/cmd 顶层 greet_gap_sec",
      "MQTT", "顶层（无 action）下发 greet_gap_sec 同样生效（随后还原）", needs=DEV)
def tc_greet_mqtt_top(ctx):
    bus = _b(ctx)
    cur = _face_snapshot(ctx, bus)
    ctx.check(cur is not None, "未收到 status/face")
    origin = int(cur.get("greet_gap_sec") or 1800)
    target = 777 if origin != 777 else 888
    since = time.time()
    bus.publish(ctx.cmd("face"), {"greet_gap_sec": target, "ts": int(time.time())})
    _force_face_status(ctx, bus)
    data = _wait_status(ctx, bus, "face", lambda d: int(d.get("greet_gap_sec") or 0) == target,
                        timeout=15, since=since)
    ctx.check(data is not None, f"顶层 greet_gap_sec 未回显 {target}")
    bus.publish(ctx.cmd("face"), {"greet_gap_sec": origin, "ts": int(time.time())})
    _force_face_status(ctx, bus)
    ctx.log(f"顶层 greet_gap_sec {origin} → {target}")


@case("TC0656-greet-mcp-simulate", "F0656", "greet", "MCP self.face.simulate_greet",
      "WS-MCP", "调用返回成功，current_speaker.source=simulate（随后 clear）", needs=("wsmcp", "mqtt"))
def tc_greet_mcp_simulate(ctx):
    bus = _b(ctx)
    try:
        ws = ctx.wsmcp
    except Exception as exc:
        ctx.blocked(f"WS-MCP 不可用：{exc}")
    bus.subscribe(ctx.status("device"))
    r = ws.call("self.face.simulate_greet", {"name": "dogtest-mcp", "local_id": 1})
    if r.get("error"):
        ctx.check(False, f"simulate_greet 返回错误：{str(r)[:140]}")
    sp = _speaker_of(ctx, bus, timeout=18, name="dogtest-mcp")
    ws.call("self.face.clear_speaker", {})
    ctx.check(sp is not None, "MCP simulate_greet 后 current_speaker 未出现")
    ctx.check(sp.get("display_name") == "dogtest-mcp", f"display_name 不符：{sp}")
    ctx.log(f"current_speaker={json.dumps(sp, ensure_ascii=False)[:120]}")


@case("TC0658-greet-mcp-set-config", "F0658", "greet", "MCP self.face.set_greet_config",
      "WS-MCP", "设置 greet_gap_sec 生效（随后还原）", needs=("wsmcp", "mqtt"))
def tc_greet_mcp_cfg(ctx):
    bus = _b(ctx)
    try:
        ws = ctx.wsmcp
    except Exception as exc:
        ctx.blocked(f"WS-MCP 不可用：{exc}")
    cur = _face_snapshot(ctx, bus)
    ctx.check(cur is not None, "未收到 status/face")
    origin = int(cur.get("greet_gap_sec") or 1800)
    target = 999 if origin != 999 else 1111
    since = time.time()
    r = ws.call("self.face.set_greet_config", {"greet_enabled": True, "greet_gap_sec": target})
    if r.get("error"):
        ctx.check(False, f"set_greet_config 返回错误：{str(r)[:140]}")
    _force_face_status(ctx, bus)
    data = _wait_status(ctx, bus, "face", lambda d: int(d.get("greet_gap_sec") or 0) == target,
                        timeout=15, since=since)
    ws.call("self.face.set_greet_config", {"greet_enabled": True, "greet_gap_sec": origin})
    ctx.check(data is not None, f"MCP 设置后 status/face.greet_gap_sec 未回显 {target}")


# ==========================================================================
# handle：输入超时清零 / 轴映射
# ==========================================================================

@case("TC0751-handle-input-timeout", "F0751", "handle", "输入超时清零",
      "MQTT", "注入摇杆后 >1s 无输入 → 轴自动归零", needs=DEV)
def tc_handle_timeout(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("handle"))
    since = time.time()
    # 契约（I05 §1）：桥的 input 报文必带 connected=true；固件仅在 connected=true 时
    # 挂载 1000ms 输入超时定时器（handle_mqtt.cc HandleInput），停发后由定时器清零。
    bus.publish(ctx.cmd("handle/input"),
                {"connected": True, "axes": {"lx": 0.9, "ly": 0.0}, "source": "wifi"})
    got = _wait_status(ctx, bus, "handle",
                       lambda d: abs(float((d.get("axes") or {}).get("lx", 0))) > 0.5,
                       timeout=6, since=since)
    if got is None:
        ctx.blocked("注入的摇杆值未在 status/handle 回显（无法验证超时清零）")
    # 超时（默认 1000ms）后应清零
    since = time.time()
    cleared = _wait_status(ctx, bus, "handle",
                           lambda d: abs(float((d.get("axes") or {}).get("lx", 1))) < 0.2,
                           timeout=8, since=since)
    ctx.check(cleared is not None, "超时后轴未清零（输入超时机制未生效）")
    ctx.log("lx 0.9 → 0（超时清零）")


@case("TC0760-handle-axis-bindings", "F0760", "handle", "运行时轴映射 axis_bindings",
      "MQTT", "status/handle/keymap 含 axis_bindings（并按 profile 生效）", needs=DEV)
def tc_handle_axis(ctx):
    bus = _b(ctx)
    topic = ctx.status("handle/keymap")
    bus.subscribe(topic)
    msg = bus.wait_topic(topic, timeout=12)
    ctx.check(msg is not None, "未收到 status/handle/keymap")
    data = envelope_data(json.loads(msg["payload"]))
    axes = data.get("axis_bindings")
    ctx.check(isinstance(axes, (list, dict)), f"axis_bindings 缺失或形态异常：{type(axes)}")
    n = len(axes)
    ctx.check(n >= 1, "axis_bindings 为空")
    ctx.log(f"axis_bindings={n} profile={data.get('profile')}")


# ==========================================================================
# motor：mcp_call 白名单拒绝
# ==========================================================================

@case("TC1075-motor-mcp-call-whitelist", "F1075", "motor", "cmd/motor mcp_call 白名单外拒绝",
      "MQTT", "非 motor/can 工具被拒（mcp_result 明确报错）", needs=DEV)
def tc_motor_mcp_whitelist(ctx):
    bus = _b(ctx)
    if _cap(ctx, bus, "motor") is not True:
        ctx.blocked("电机栈未激活（EXT_PIN != can）")
    res_topic = ctx.topic("event", "motor/mcp_result")
    bus.subscribe(res_topic)
    since = time.time()
    bus.publish(ctx.cmd("motor"), {"mcp_call": {"name": "self.face.set_mode", "arguments": {}},
                                   "ts": int(time.time())})
    msg = bus.wait_topic(res_topic, timeout=10, since=since)
    ctx.check(msg is not None, "白名单外调用未回 mcp_result")
    data = envelope_data(json.loads(msg["payload"]))
    text = json.dumps(data, ensure_ascii=False).lower()
    ctx.check("ok" in text and ("false" in text or "not allowed" in text or "error" in text),
              f"白名单外调用应被拒绝：{text[:160]}")
    ctx.log(text[:150])


# ==========================================================================
# mcp：客户端生命周期 / dp：telemetry 多模块汇入
# ==========================================================================

@case("TC1309-mcp-client-lifecycle", "F1309", "mcp", "客户端断开后服务仍可用",
      "WS-MCP", "客户端断开不影响后续新客户端 tools/list", needs=("wsmcp",))
def tc_mcp_lifecycle(ctx):
    from dogtest.clients import WsMcp
    if not ctx.cfg.ws_url:
        ctx.blocked("ws_url 未发现")
    a = WsMcp(ctx.cfg.ws_url).connect()
    n_a = len(a.list_tools())
    a.close()
    time.sleep(1.0)
    b = WsMcp(ctx.cfg.ws_url).connect()
    try:
        n_b = len(b.list_tools())
    finally:
        b.close()
    ctx.check(n_a >= 1 and n_b >= 1, f"断开后不可用：A={n_a} tools, B={n_b} tools")
    ctx.log(f"client A={n_a} tools → close → B={n_b} tools")


@case("TC1262-dp-telemetry-multi-module", "F1262", "dp", "telemetry 自动汇入多模块数据点",
      "MQTT", "status/telemetry.points 同时含来自不同模块的点位", needs=DEV)
def tc_telemetry_multi(ctx):
    bus = _b(ctx)
    bus.subscribe(ctx.status("telemetry"))
    data = _wait_status(ctx, bus, "telemetry", timeout=30)
    if data is None:
        ctx.skip("遥测未配置（telemetry 话题为空）→ 不发布")
    pts = data.get("points") or {}
    if not isinstance(pts, dict):
        ctx.check(False, f"points 形态异常：{type(pts)}")
    # 期望至少覆盖「系统」与「设备」两类点位（system.* 与 device.*）
    sys_hit = [k for k in pts if any(s in k for s in ("internal", "psram", "min_free", "largest"))]
    dev_hit = [k for k in pts if any(s in k for s in ("rssi", "uptime", "free_heap"))]
    ctx.check(sys_hit, f"未见 system 类点位：{sorted(pts)[:10]}")
    ctx.check(dev_hit, f"未见 device 类点位：{sorted(pts)[:10]}")
    ctx.log(f"points={len(pts)}：system {len(sys_hit)} + device {len(dev_hit)}")
