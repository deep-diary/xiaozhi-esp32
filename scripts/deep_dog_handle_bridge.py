#!/usr/bin/env python3
"""PC gamepad → MQTT handle/input bridge for deep-dog.

Reads a local controller (PS4 / Xbox / generic via pygame) and publishes
normalized snapshots to deepdiary/deep-dog/{device_id}/handle/input.

Dependencies:
  pip3 install paho-mqtt pygame
  # optional, only for --touchpad-xy (DS4 HID full read):
  pip3 install hidapi

Credentials (do not commit):
  export DEEP_DOG_MQTT_USER=...
  export DEEP_DOG_MQTT_PASS=...

Examples:
  /usr/bin/python3 scripts/deep_dog_handle_bridge.py --via lan --layout auto
  /usr/bin/python3 scripts/deep_dog_handle_bridge.py --via lan --layout ds4_sdl
  python3 scripts/deep_dog_handle_bridge.py --via lan --touchpad-xy
  /usr/bin/python3 scripts/deep_dog_handle_bridge.py --list-joysticks
  /usr/bin/python3 scripts/deep_dog_handle_bridge.py --probe
  /usr/bin/python3 scripts/deep_dog_handle_bridge.py --via lan --wait-pad

Profiles:
  ds4_sdl   — pygame 2.x「PS4 Controller」(macOS/Windows 常见)：轴 0-3 摇杆、4-5 扳机
  ds4_linux — Linux HID 标注图（L2 在 axis2；面键 △=2 □=3）
  xbox      — 常见 XInput
  auto      — 按名称 / 空闲轴启发式选择

--touchpad-xy (default off):
  macOS + DS4 USB → hidapi 全量读同一份 report（按键+摇杆+touchpad XY）。
  关闭时行为与现网一致（仅 pygame + buttons.touch）。
  探测脚本：scripts/deep_dog_ds4_touchpad_probe.py

抽象极性（I01）：lx/rx 右为正；ly/ry 下为正（前推 ly<0）。
Touchpad（I06）：x 左→右、y 上→下，归一化 [0,1]。
"""

from __future__ import annotations

import argparse
import json
import os
import ssl
import sys
import time
from datetime import datetime
from urllib.parse import urlparse

_SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPTS_DIR not in sys.path:
    sys.path.insert(0, _SCRIPTS_DIR)

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("missing dependency: pip3 install paho-mqtt", file=sys.stderr)
    sys.exit(2)

pygame = None  # type: ignore


def require_pygame():
    """Lazy-import pygame (not needed for --touchpad-xy HID path)."""
    global pygame
    if pygame is not None:
        return pygame
    try:
        import pygame as _pygame
    except ImportError:
        print("missing dependency: pip3 install pygame", file=sys.stderr)
        sys.exit(2)
    pygame = _pygame
    return pygame


WEB_WSS_DEFAULT = "wss://mqtt-ws.deep-diary.com/mqtt"
LAN_HOST_DEFAULT = "192.168.31.25"
LAN_PORT_DEFAULT = 1883

LAYOUT_CHOICES = ("auto", "ds4_sdl", "ds4_linux", "ds4", "xbox")


def ts() -> str:
    return datetime.now().strftime("%H:%M:%S")


def make_client(client_id: str, transport: str):
    try:
        return mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=client_id,
            transport=transport,
        )
    except Exception:
        return mqtt.Client(client_id=client_id, transport=transport)


def clamp(v: float, lo: float, hi: float) -> float:
    return lo if v < lo else hi if v > hi else v


def axis_deadzone(v: float, dz: float = 0.08) -> float:
    return 0.0 if abs(v) < dz else clamp(v, -1.0, 1.0)


def detect_layout(joy, explicit: str = "auto") -> str:
    """Pick HID profile. `ds4` is alias for ds4_sdl (pygame2 官方表)."""
    require_pygame()
    if explicit == "ds4":
        return "ds4_sdl"
    if explicit in ("ds4_sdl", "ds4_linux", "xbox"):
        return explicit

    name = (joy.get_name() or "").lower()
    n_axes = joy.get_numaxes()
    n_buttons = joy.get_numbuttons()
    pygame.event.pump()
    idle = [joy.get_axis(i) if i < n_axes else 0.0 for i in range(min(n_axes, 8))]

    if any(k in name for k in ("xbox", "x-box", "xinput", "360")):
        return "xbox"

    # 扳机静置 ≈ -1：若 axis4/5 是扳机而 axis2≈0 → pygame2 SDL 布局
    if n_axes >= 6 and n_buttons >= 14:
        a2 = abs(idle[2]) if len(idle) > 2 else 0
        a4 = idle[4] if len(idle) > 4 else 0
        if a2 < 0.25 and a4 < -0.5:
            return "ds4_sdl"
        if a2 < -0.5:
            return "ds4_linux"

    if "wireless controller" in name or "dualshock" in name or "dualsense" in name:
        return "ds4_linux" if n_axes >= 8 else "ds4_sdl"

    if "ps4" in name or "ps5" in name:
        return "ds4_sdl"

    return "ds4_sdl"


def read_snapshot(joy, layout: str) -> dict:
    """Map pygame joystick → deep-dog abstract snapshot."""
    require_pygame()
    pygame.event.pump()
    n_axes = joy.get_numaxes()
    n_buttons = joy.get_numbuttons()
    n_hats = joy.get_numhats()

    def raw_ax(i: int) -> float:
        return joy.get_axis(i) if i < n_axes else 0.0

    def btn(i: int) -> bool:
        return bool(joy.get_button(i)) if i < n_buttons else False

    def trigger01(axis_i: int) -> float:
        if axis_i >= n_axes:
            return 0.0
        return clamp((raw_ax(axis_i) + 1.0) * 0.5, 0.0, 1.0)

    ps = l3 = r3 = touch = False
    dx = dy = 0.0

    if layout == "xbox":
        lx = axis_deadzone(raw_ax(0))
        ly = axis_deadzone(raw_ax(1))
        if n_axes >= 6:
            rx = axis_deadzone(raw_ax(3))
            ry = axis_deadzone(raw_ax(4))
            l2 = trigger01(2)
            r2 = trigger01(5)
        else:
            rx = axis_deadzone(raw_ax(2))
            ry = axis_deadzone(raw_ax(3))
            l2 = r2 = 0.0
        a, b, x, y = btn(0), btn(1), btn(2), btn(3)
        l1, r1 = btn(4), btn(5)
        select, start = btn(6), btn(7)
        ps = btn(8)
        l3, r3 = btn(9), btn(10)
        if n_hats > 0:
            hx, hy = joy.get_hat(0)
            dx, dy = float(hx), float(-hy)

    elif layout == "ds4_linux":
        # Linux 标注图：0 LX 1 LY 2 L2 3 RX 4 RY 5 R2 6/7 D-pad
        # 原始左/上 = +1 → 取反到抽象右/下为正
        # 键：0✕ 1○ 2△ 3□ 4 L1 5 R1 8 Share 9 Options 10 PS 11 L3 12 R3
        lx = axis_deadzone(-raw_ax(0))
        ly = axis_deadzone(-raw_ax(1))
        rx = axis_deadzone(-raw_ax(3))
        ry = axis_deadzone(-raw_ax(4))
        l2 = trigger01(2)
        r2 = trigger01(5)
        a, b = btn(0), btn(1)
        y, x = btn(2), btn(3)
        l1, r1 = btn(4), btn(5)
        select, start = btn(8), btn(9)
        ps, l3, r3 = btn(10), btn(11), btn(12)
        dx = axis_deadzone(-raw_ax(6)) if n_axes > 6 else 0.0
        dy = axis_deadzone(-raw_ax(7)) if n_axes > 7 else 0.0

    else:
        # ds4_sdl — pygame 2 官方「PS4 Controller」表（macOS 实测）
        # axes: 0/1 左杆 L→R U→D；2/3 右杆；4/5 L2/R2（松开-1 按下+1）
        # buttons: 0✕ 1○ 2□ 3△ 4 Share 5 PS 6 Options 7 L3 8 R3 9 L1 10 R1
        #          11↑ 12↓ 13← 14→ 15 Touch
        # 极性：水平已是右为正；垂直在本机实测与抽象「下为正」相反 → ly/ry 取反
        lx = axis_deadzone(raw_ax(0))
        ly = axis_deadzone(-raw_ax(1))
        rx = axis_deadzone(raw_ax(2))
        ry = axis_deadzone(-raw_ax(3))
        l2 = trigger01(4)
        r2 = trigger01(5)
        a, b = btn(0), btn(1)
        x, y = btn(2), btn(3)  # □=x, △=y
        select, ps, start = btn(4), btn(5), btn(6)
        l3, r3 = btn(7), btn(8)
        l1, r1 = btn(9), btn(10)
        dpad_up, dpad_down = btn(11), btn(12)
        dpad_left, dpad_right = btn(13), btn(14)
        touch = btn(15)

    if layout != "ds4_sdl":
        dpad_up = dy < -0.5
        dpad_down = dy > 0.5
        dpad_left = dx < -0.5
        dpad_right = dx > 0.5

    return {
        "connected": True,
        "source": "wifi",
        "axes": {"lx": lx, "ly": ly, "rx": rx, "ry": ry},
        "buttons": {
            "a": a,
            "b": b,
            "x": x,
            "y": y,
            "l1": l1,
            "r1": r1,
            "l2": l2,
            "r2": r2,
            "select": select,
            "start": start,
            "ps": ps,
            "l3": l3,
            "r3": r3,
            "touch": touch,
            "dpad_up": dpad_up,
            "dpad_down": dpad_down,
            "dpad_left": dpad_left,
            "dpad_right": dpad_right,
        },
        "ts": int(time.time()),
    }


def snapshot_equal(a: dict | None, b: dict | None, eps: float = 0.02) -> bool:
    if a is None or b is None:
        return False
    if a.get("connected") != b.get("connected"):
        return False
    aa, ba = a.get("axes", {}), b.get("axes", {})
    for k in ("lx", "ly", "rx", "ry"):
        if abs(float(aa.get(k, 0)) - float(ba.get(k, 0))) > eps:
            return False
    ab, bb = a.get("buttons", {}), b.get("buttons", {})
    for k in (
        "a", "b", "x", "y", "l1", "r1", "start", "select", "ps", "l3", "r3", "touch",
        "dpad_up", "dpad_down", "dpad_left", "dpad_right",
    ):
        if bool(ab.get(k)) != bool(bb.get(k)):
            return False
    for k in ("l2", "r2"):
        if abs(float(ab.get(k, 0)) - float(bb.get(k, 0))) > eps:
            return False
    at, bt = a.get("touchpad"), b.get("touchpad")
    if (at is None) != (bt is None):
        return False
    if at is not None and bt is not None:
        if bool(at.get("active")) != bool(bt.get("active")):
            return False
        if abs(float(at.get("x", 0)) - float(bt.get("x", 0))) > eps:
            return False
        if abs(float(at.get("y", 0)) - float(bt.get("y", 0))) > eps:
            return False
        if int(at.get("fingers", 0)) != int(bt.get("fingers", 0)):
            return False
    return True


def empty_buttons() -> dict:
    return {
        "a": False,
        "b": False,
        "x": False,
        "y": False,
        "l1": False,
        "r1": False,
        "l2": 0.0,
        "r2": 0.0,
        "start": False,
        "select": False,
        "ps": False,
        "l3": False,
        "r3": False,
        "touch": False,
        "dpad_up": False,
        "dpad_down": False,
        "dpad_left": False,
        "dpad_right": False,
    }


def offline_snapshot(*, with_touchpad: bool = False) -> dict:
    snap = {
        "connected": False,
        "source": "wifi",
        "axes": {"lx": 0.0, "ly": 0.0, "rx": 0.0, "ry": 0.0},
        "buttons": empty_buttons(),
        "ts": int(time.time()),
    }
    if with_touchpad:
        snap["touchpad"] = {"active": False, "x": 0.0, "y": 0.0, "fingers": 0}
    return snap


def public_snapshot(snap: dict) -> dict:
    """Strip internal/debug keys before MQTT publish."""
    allowed = {"connected", "source", "axes", "buttons", "touchpad", "ts"}
    out = {k: v for k, v in snap.items() if k in allowed}
    return out


def acquire_joystick(preferred_index: int, layout_arg: str, *, hard: bool = False):
    """Open joystick. hard=True 会 quit/init 子系统（仅在确认掉线后用）。"""
    require_pygame()
    if hard:
        pygame.joystick.quit()
        pygame.joystick.init()
        # 丢掉 quit/init 产生的 ADDED/REMOVED，避免自激循环
        pygame.event.clear()
    elif not pygame.joystick.get_init():
        pygame.joystick.init()

    count = pygame.joystick.get_count()
    if count <= 0:
        return None, None
    idx = preferred_index if 0 <= preferred_index < count else 0
    joy = pygame.joystick.Joystick(idx)
    joy.init()
    layout = detect_layout(joy, layout_arg)
    print(
        f"[{ts()}] joystick[{idx}] {joy.get_name()} "
        f"layout={layout} axes={joy.get_numaxes()} buttons={joy.get_numbuttons()}"
    )
    pygame.event.clear()
    return joy, layout


def joystick_alive(joy) -> bool:
    if joy is None:
        return False
    require_pygame()
    try:
        if not pygame.joystick.get_init() or pygame.joystick.get_count() <= 0:
            return False
        _ = joy.get_numaxes()
        return True
    except Exception:
        return False


def publish_snap(client, topic: str, snap: dict) -> None:
    client.publish(
        topic,
        json.dumps(public_snapshot(snap), separators=(",", ":")),
        qos=0,
        retain=False,
    )


def try_import_hid():
    try:
        import hid  # type: ignore

        return hid
    except ImportError:
        return None


def acquire_ds4_hid(hid_mod, path: bytes | None = None):
    """Open DS4 via hidapi for --touchpad-xy. Returns (dev, info) or (None, None)."""
    from deep_dog_ds4_hid import open_ds4_device

    return open_ds4_device(hid_mod, path=path)


def read_hid_snapshot(dev) -> dict | None:
    from deep_dog_ds4_hid import parse_ds4_usb_report, read_ds4_report

    data = read_ds4_report(dev)
    if not data:
        return None
    return parse_ds4_usb_report(data)


def run_touchpad_xy_loop(
    client,
    input_topic: str,
    *,
    hz: float,
    heartbeat_ms: int,
    reconnect_ms: int,
    wait_pad: bool,
    hid_path: str | None,
) -> int:
    """HID-only bridge path: buttons + axes + touchpad from one DS4 report."""
    hid_mod = try_import_hid()
    if hid_mod is None:
        print(
            "missing dependency for --touchpad-xy: pip3 install hidapi "
            f"(interpreter={sys.executable})",
            file=sys.stderr,
        )
        return 2

    path = hid_path.encode("utf-8") if hid_path else None
    interval = 1.0 / max(1.0, hz)
    heartbeat_s = max(0.05, heartbeat_ms / 1000.0)
    reconnect_s = max(0.2, reconnect_ms / 1000.0)

    dev, info = acquire_ds4_hid(hid_mod, path=path)
    if dev is None:
        if not wait_pad:
            print(
                "no DS4 found for --touchpad-xy; plug USB DualShock 4 "
                "(or pass --wait-pad)",
                file=sys.stderr,
            )
            return 1
        print(f"[{ts()}] --touchpad-xy: no DS4 yet; waiting…")

    last_snap = None
    last_pub = 0.0
    last_rescan = 0.0
    pad_online = False
    stale_reads = 0
    STALE_LIMIT = 200  # ~ nonblocking empty reads before treat as disconnect

    if info is not None:
        pid = int(info.get("product_id") or 0)
        print(
            f"[{ts()}] --touchpad-xy HID mode pid=0x{pid:04X} "
            f"product={info.get('product_string')!r}"
        )
        pad_online = True

    try:
        while True:
            now = time.time()
            if not pad_online or dev is None:
                if now - last_rescan >= reconnect_s:
                    last_rescan = now
                    if dev is not None:
                        try:
                            dev.close()
                        except Exception:
                            pass
                        dev = None
                    dev, info = acquire_ds4_hid(hid_mod, path=path)
                    if dev is not None:
                        print(f"[{ts()}] DS4 reconnected (HID)")
                        pad_online = True
                        last_snap = None
                        stale_reads = 0
                time.sleep(0.05)
                continue

            snap = read_hid_snapshot(dev)
            if snap is None:
                stale_reads += 1
                if stale_reads >= STALE_LIMIT:
                    print(f"[{ts()}] HID stale — publish connected=false; will rescan")
                    pad_online = False
                    off = offline_snapshot(with_touchpad=True)
                    publish_snap(client, input_topic, off)
                    last_pub = now
                    last_snap = off
                    try:
                        dev.close()
                    except Exception:
                        pass
                    dev = None
                time.sleep(0.001)
                continue

            stale_reads = 0
            pad_online = True
            changed = not snapshot_equal(snap, last_snap)
            due_change = changed and (now - last_pub >= interval)
            due_heartbeat = (now - last_pub) >= heartbeat_s
            if due_change or due_heartbeat:
                snap = {**snap, "ts": int(now)}
                publish_snap(client, input_topic, snap)
                last_pub = now
                last_snap = snap
            time.sleep(0.001)
    except KeyboardInterrupt:
        print(f"[{ts()}] stopping; publish connected=false")
        publish_snap(client, input_topic, offline_snapshot(with_touchpad=True))
        time.sleep(0.2)
    finally:
        if dev is not None:
            try:
                dev.close()
            except Exception:
                pass
    return 0


def probe_joystick(index: int, seconds: float = 20.0) -> int:
    require_pygame()
    pygame.init()
    pygame.joystick.init()
    if pygame.joystick.get_count() <= 0:
        print("no joystick", file=sys.stderr)
        return 1
    joy = pygame.joystick.Joystick(index)
    joy.init()
    layout = detect_layout(joy, "auto")
    print(
        f"[{ts()}] probe [{index}] {joy.get_name()} "
        f"axes={joy.get_numaxes()} buttons={joy.get_numbuttons()} hats={joy.get_numhats()} "
        f"auto_layout={layout}"
    )
    pygame.event.pump()
    idle = [round(joy.get_axis(i), 3) for i in range(joy.get_numaxes())]
    print(f"IDLE axes: {idle}")
    print("Press buttons / move sticks…")
    prev_btns: set[int] = set()
    t0 = time.time()
    while time.time() - t0 < seconds:
        pygame.event.pump()
        btns = {i for i in range(joy.get_numbuttons()) if joy.get_button(i)}
        if btns != prev_btns:
            down = sorted(btns - prev_btns)
            up = sorted(prev_btns - btns)
            if down:
                print(f"  DOWN {down}")
            if up:
                print(f"  UP   {up}")
            prev_btns = btns
        moving = {
            i: round(joy.get_axis(i), 2)
            for i in range(joy.get_numaxes())
            if abs(joy.get_axis(i)) > 0.35
        }
        if moving:
            print(f"  AXES {moving}")
        time.sleep(0.05)
    pygame.quit()
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="PC gamepad → deep-dog handle/input MQTT bridge")
    ap.add_argument("--via", choices=["web", "lan"], default="lan")
    ap.add_argument("--wss", default=WEB_WSS_DEFAULT)
    ap.add_argument("--broker", default=LAN_HOST_DEFAULT)
    ap.add_argument("--port", type=int, default=LAN_PORT_DEFAULT)
    ap.add_argument("--device-id", default="dev")
    ap.add_argument("--username", default=os.environ.get("DEEP_DOG_MQTT_USER", ""))
    ap.add_argument("--password", default=os.environ.get("DEEP_DOG_MQTT_PASS", ""))
    ap.add_argument("--joystick", type=int, default=0, help="pygame joystick index")
    ap.add_argument(
        "--layout",
        choices=LAYOUT_CHOICES,
        default="auto",
        help="HID profile; Mac PS4 用 ds4_sdl（auto 会识别）",
    )
    ap.add_argument("--hz", type=float, default=40.0, help="max publish rate on change")
    ap.add_argument(
        "--heartbeat-ms",
        type=int,
        default=150,
        help="re-publish idle snapshot at least this often (device clears after 500ms silence)",
    )
    ap.add_argument(
        "--reconnect-ms",
        type=int,
        default=1000,
        help="when pad missing, rescan interval (ms)",
    )
    ap.add_argument("--list-joysticks", action="store_true")
    ap.add_argument("--probe", action="store_true", help="print raw axes/buttons then exit")
    ap.add_argument(
        "--wait-pad",
        action="store_true",
        help="if no joystick at start, wait instead of exiting",
    )
    ap.add_argument(
        "--touchpad-xy",
        action="store_true",
        help="DS4 hidapi full-read: buttons+axes+touchpad XY (default off; needs hidapi)",
    )
    ap.add_argument(
        "--hid-path",
        default=None,
        help="optional hidapi path for --touchpad-xy (from probe --list)",
    )
    args = ap.parse_args()

    if args.probe:
        return probe_joystick(args.joystick)

    joy, layout = None, None
    if args.touchpad_xy:
        if args.list_joysticks:
            hid_mod = try_import_hid()
            if hid_mod is None:
                print("pip3 install hidapi", file=sys.stderr)
                return 2
            from deep_dog_ds4_hid import is_ds4, list_sony_devices

            devices = list_sony_devices(hid_mod)
            print(f"Sony HID devices: {len(devices)}")
            for i, d in enumerate(devices):
                mark = "DS4" if is_ds4(d) else "other"
                print(
                    f"  [{i}] {mark} pid=0x{int(d.get('product_id') or 0):04X} "
                    f"product={d.get('product_string')!r}"
                )
            return 0
        layout = "ds4_hid"
    else:
        require_pygame()
        pygame.init()
        pygame.joystick.init()
        count = pygame.joystick.get_count()
        if args.list_joysticks:
            print(f"joysticks: {count}")
            for i in range(count):
                j = pygame.joystick.Joystick(i)
                j.init()
                print(
                    f"  [{i}] {j.get_name()} axes={j.get_numaxes()} "
                    f"buttons={j.get_numbuttons()}"
                )
            return 0

        joy, layout = acquire_joystick(args.joystick, args.layout)
        if joy is None:
            if not args.wait_pad:
                print(
                    "no joystick found; plug in PS4/Xbox (or pass --wait-pad)",
                    file=sys.stderr,
                )
                return 1
            print(f"[{ts()}] no joystick yet; waiting (--wait-pad)…")

    prefix = f"deepdiary/deep-dog/{args.device_id}"
    input_topic = f"{prefix}/handle/input"
    status_topic = f"{prefix}/handle/status"

    use_ws = args.via == "web"
    transport = "websockets" if use_ws else "tcp"
    client_id = f"deep-dog-handle-bridge-{int(time.time())}"
    connected = {"ok": False}

    def on_connect(client, userdata, flags, reason_code, properties=None):
        rc = reason_code if isinstance(reason_code, int) else getattr(reason_code, "value", reason_code)
        if rc != 0:
            print(f"[{ts()}] CONNECT failed rc={rc}", file=sys.stderr)
            return
        connected["ok"] = True
        client.subscribe(status_topic, qos=0)
        print(f"[{ts()}] CONNECTED; PUB {input_topic}; SUB {status_topic}")

    def on_message(client, userdata, msg):
        payload = msg.payload.decode("utf-8", errors="replace")
        summary = ""
        try:
            o = json.loads(payload)
            tp = o.get("touchpad")
            bt = (o.get("buttons") or {}).get("touch")
            ax = o.get("axes") or {}
            if isinstance(tp, dict):
                summary = (
                    f" touchpad={{active:{1 if tp.get('active') else 0} "
                    f"x:{float(tp.get('x', 0)):.2f} y:{float(tp.get('y', 0)):.2f} "
                    f"fingers:{int(tp.get('fingers', 0))}}}"
                )
            else:
                summary = " touchpad=<absent>"
            summary += (
                f" touch_btn={1 if bt else 0}"
                f" lx={float(ax.get('lx', 0)):.2f} ly={float(ax.get('ly', 0)):.2f}"
            )
        except Exception:
            summary = ""
        preview = payload if len(payload) <= 160 else payload[:160] + "..."
        print(f"[{ts()}] STATUS{summary} | {preview}")

    client = make_client(client_id, transport)
    if args.username:
        client.username_pw_set(args.username, args.password)
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        if use_ws:
            parsed = urlparse(args.wss)
            host = parsed.hostname or "mqtt-ws.deep-diary.com"
            port = parsed.port or (443 if parsed.scheme == "wss" else 80)
            path = parsed.path or "/mqtt"
            client.ws_set_options(path=path)
            if parsed.scheme == "wss":
                client.tls_set(cert_reqs=ssl.CERT_REQUIRED)
                client.tls_insecure_set(False)
            client.connect(host, port, keepalive=30)
        else:
            client.connect(args.broker, args.port, keepalive=30)
    except Exception as e:
        print(f"connect error: {e}", file=sys.stderr)
        return 1

    client.loop_start()
    deadline = time.time() + 10
    while time.time() < deadline and not connected["ok"]:
        time.sleep(0.05)
    if not connected["ok"]:
        print("MQTT connect timeout", file=sys.stderr)
        client.loop_stop()
        return 1

    if args.touchpad_xy:
        print(
            f"[{ts()}] --touchpad-xy HID full-read; "
            f"on-change @{args.hz:.0f}Hz; heartbeat every {args.heartbeat_ms}ms"
        )
        try:
            return run_touchpad_xy_loop(
                client,
                input_topic,
                hz=args.hz,
                heartbeat_ms=args.heartbeat_ms,
                reconnect_ms=args.reconnect_ms,
                wait_pad=args.wait_pad,
                hid_path=args.hid_path,
            )
        finally:
            client.loop_stop()
            client.disconnect()

    interval = 1.0 / max(args.hz, 1.0)
    heartbeat_s = max(args.heartbeat_ms, 50) / 1000.0
    reconnect_s = max(args.reconnect_ms, 200) / 1000.0
    # 硬重扫最短间隔，避免 Mac 上 ADDED 事件风暴
    hard_rescan_min_s = max(reconnect_s, 2.0)
    last_pub = 0.0
    last_snap = None
    last_rescan = 0.0
    pad_online = joy is not None
    need_hard_rescan = False
    print(
        f"[{ts()}] publish on-change @{args.hz:.0f}Hz; heartbeat every {args.heartbeat_ms}ms; "
        f"auto-reconnect every {args.reconnect_ms}ms when pad lost"
    )

    joy_added = getattr(pygame, "JOYDEVICEADDED", None)
    joy_removed = getattr(pygame, "JOYDEVICEREMOVED", None)

    try:
        while True:
            now = time.time()
            for ev in pygame.event.get():
                if joy_removed is not None and ev.type == joy_removed:
                    # 已在线时忽略瞬时 REMOVED（Mac 上常与 ADDED 成对刷屏）
                    if not pad_online or not joystick_alive(joy):
                        print(f"[{ts()}] JOYDEVICEREMOVED — waiting for pad…")
                        joy = None
                        pad_online = False
                        need_hard_rescan = True
                        off = offline_snapshot()
                        publish_snap(client, input_topic, off)
                        last_pub = now
                        last_snap = off
                elif joy_added is not None and ev.type == joy_added:
                    # 已在线：忽略 ADDED，禁止 quit/init（否则会死循环 rescanning）
                    if pad_online and joystick_alive(joy):
                        continue
                    if now - last_rescan < hard_rescan_min_s:
                        continue
                    print(f"[{ts()}] JOYDEVICEADDED — soft open…")
                    last_rescan = now
                    joy, layout = acquire_joystick(
                        args.joystick, args.layout, hard=need_hard_rescan
                    )
                    if joy is not None:
                        pad_online = True
                        need_hard_rescan = False
                        last_snap = None

            if not joystick_alive(joy):
                if pad_online:
                    print(f"[{ts()}] pad lost/stale — publish connected=false; will rescan")
                    pad_online = False
                    need_hard_rescan = True
                    off = offline_snapshot()
                    publish_snap(client, input_topic, off)
                    last_pub = now
                    last_snap = off
                    joy = None
                if now - last_rescan >= reconnect_s:
                    last_rescan = now
                    use_hard = need_hard_rescan
                    joy, layout = acquire_joystick(
                        args.joystick, args.layout, hard=use_hard
                    )
                    if joy is None and use_hard:
                        # 硬扫也失败：下次再试
                        pass
                    elif joy is None:
                        # 软开失败，下次硬扫
                        need_hard_rescan = True
                    else:
                        print(
                            f"[{ts()}] pad reconnected"
                            + (" (hard)" if use_hard else "")
                        )
                        pad_online = True
                        need_hard_rescan = False
                        last_snap = None
                time.sleep(0.05)
                continue

            try:
                snap = read_snapshot(joy, layout=layout or "ds4_sdl")
            except Exception as e:
                print(f"[{ts()}] read_snapshot failed: {e}; treating as disconnect")
                joy = None
                pad_online = False
                need_hard_rescan = True
                continue

            pad_online = True
            changed = not snapshot_equal(snap, last_snap)
            due_change = changed and (now - last_pub >= interval)
            due_heartbeat = (now - last_pub) >= heartbeat_s
            if due_change or due_heartbeat:
                snap = {**snap, "ts": int(now)}
                publish_snap(client, input_topic, snap)
                last_pub = now
                last_snap = snap
            time.sleep(0.01)
    except KeyboardInterrupt:
        print(f"[{ts()}] stopping; publish connected=false")
        publish_snap(client, input_topic, offline_snapshot())
        time.sleep(0.2)
    finally:
        client.loop_stop()
        client.disconnect()
        pygame.quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
