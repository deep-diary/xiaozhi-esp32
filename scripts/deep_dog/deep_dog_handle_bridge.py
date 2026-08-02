#!/usr/bin/env python3
"""PC gamepad → MQTT handle/input bridge for deep-dog.

Default: DS4 hidapi full-read (axes/buttons/touchpad/motion) + subscribe
handle/cmd for lightbar/rumble feedback (I09).
Fallback: --no-touchpad-xy → pygame（Xbox/通用输入；同样订 handle/cmd output 震动）。

Dependencies:
  pip3 install paho-mqtt hidapi
  # for --no-touchpad-xy / Xbox:
  pip3 install pygame

Credentials (do not commit):
  export DEEP_DOG_MQTT_USER=...
  export DEEP_DOG_MQTT_PASS=...

Examples:
  python3 scripts/deep_dog/deep_dog_handle_bridge.py --via lan
  python3 scripts/deep_dog/deep_dog_handle_bridge.py --via lan --layout xbox
  python3 scripts/deep_dog/deep_dog_handle_bridge.py --probe-output
  python3 scripts/deep_dog/deep_dog_handle_bridge.py --probe-xbox-rumble
  python3 scripts/deep_dog/deep_dog_handle_bridge.py --via lan --wait-pad

抽象极性（I01）：lx/rx 右为正；ly/ry 下为正（前推 ly<0）。
Touchpad（I06）/ Motion（I07）/ Output（I09）：DS4=HID；Xbox 震=pygame rumble。
macOS：Xbox 震动优先蓝牙连 Mac（USB 有线常不震）。
"""

from __future__ import annotations

import argparse
import json
import os
import ssl
import sys
import threading
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

LAYOUT_CHOICES = ("auto", "ds4_sdl", "ds4_linux", "ds4", "xbox", "xbox_sdl", "xbox_xinput")


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


def xbox_uses_sdl_axes(joy) -> bool:
    """True if this Xbox pad exposes SDL/macOS axes (LT/RT on 4/5), not XInput (LT on 2).

    Mac「Xbox Series X Controller」实测 idle：
      axes ≈ [0, 0, 0, 0, -1, -1]  → 2/3=右杆, 4/5=扳机
    误用 XInput 表时静置会出现 ry≈-1、l2≈0.5（与 I03 误用 ds4_linux 同源）。
    """
    require_pygame()
    pygame.event.pump()
    n_axes = joy.get_numaxes()
    n_buttons = joy.get_numbuttons()
    n_hats = joy.get_numhats()
    if n_axes < 6:
        return False
    a2 = joy.get_axis(2)
    a4 = joy.get_axis(4)
    # XInput 静置：LT(axis2)≈-1，RX(axis3)≈0
    if a2 < -0.5 and abs(joy.get_axis(3)) < 0.35:
        return False
    # SDL 静置：RX≈0 且 LT≈-1；或 Mac GC：无 hat + 多键（扳机尚未泵到 -1 时）
    if abs(a2) < 0.35 and a4 < -0.5:
        return True
    if n_hats == 0 and n_buttons >= 14:
        return True
    return False


def detect_layout(joy, explicit: str = "auto") -> str:
    """Pick HID profile. `ds4`→ds4_sdl；`xbox`→按静置轴选 xbox_sdl / xbox_xinput。"""
    require_pygame()
    if explicit == "ds4":
        return "ds4_sdl"
    if explicit == "xbox":
        return "xbox_sdl" if xbox_uses_sdl_axes(joy) else "xbox_xinput"
    if explicit in ("ds4_sdl", "ds4_linux", "xbox_sdl", "xbox_xinput"):
        return explicit

    name = (joy.get_name() or "").lower()
    n_axes = joy.get_numaxes()
    n_buttons = joy.get_numbuttons()
    pygame.event.pump()
    idle = [joy.get_axis(i) if i < n_axes else 0.0 for i in range(min(n_axes, 8))]

    if any(k in name for k in ("xbox", "x-box", "xinput", "360")):
        return "xbox_sdl" if xbox_uses_sdl_axes(joy) else "xbox_xinput"

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

    # Resolve alias: legacy --layout xbox
    if layout == "xbox":
        layout = "xbox_sdl" if xbox_uses_sdl_axes(joy) else "xbox_xinput"

    ps = l3 = r3 = touch = False
    dx = dy = 0.0
    dpad_from_hat = False
    dpad_up = dpad_down = dpad_left = dpad_right = False

    if layout == "xbox_xinput":
        # Linux / 经典 XInput：0 LX 1 LY 2 LT 3 RX 4 RY 5 RT；D-pad 常为 hat
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
            dpad_from_hat = True

    elif layout == "xbox_sdl":
        # macOS pygame/SDL「Xbox Series X Controller」实测（与 ds4_sdl 轴下标同构）：
        # axes: 0/1 左杆；2/3 右杆；4/5 LT/RT（松开 -1，按下 +1）
        # buttons: 0A 1B 2X 3Y 4 View 5 Guide 6 Menu 7 L3 8 R3 9 LB 10 RB
        #          11↑ 12↓ 13← 14→ （hats=0）
        # 极性：SDL 已是右为正、上为负 → 直接对齐 I01（下为正），勿再取反
        lx = axis_deadzone(raw_ax(0))
        ly = axis_deadzone(raw_ax(1))
        rx = axis_deadzone(raw_ax(2))
        ry = axis_deadzone(raw_ax(3))
        l2 = trigger01(4)
        r2 = trigger01(5)
        a, b, x, y = btn(0), btn(1), btn(2), btn(3)
        select, ps, start = btn(4), btn(5), btn(6)
        l3, r3 = btn(7), btn(8)
        l1, r1 = btn(9), btn(10)
        dpad_up, dpad_down = btn(11), btn(12)
        dpad_left, dpad_right = btn(13), btn(14)

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
        dpad_from_hat = True

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

    if dpad_from_hat:
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
        ac, bc = at.get("contacts"), bt.get("contacts")
        if isinstance(ac, list) or isinstance(bc, list):
            ac = ac if isinstance(ac, list) else []
            bc = bc if isinstance(bc, list) else []
            if len(ac) != len(bc):
                return False
            for ca, cb in zip(ac, bc):
                if bool(ca.get("active")) != bool(cb.get("active")):
                    return False
                if abs(float(ca.get("x", 0)) - float(cb.get("x", 0))) > eps:
                    return False
                if abs(float(ca.get("y", 0)) - float(cb.get("y", 0))) > eps:
                    return False
    am, bm = a.get("motion"), b.get("motion")
    if (am is None) != (bm is None):
        return False
    if isinstance(am, dict) and isinstance(bm, dict):
        # IMU is noisy; do NOT let tiny gyro jitter force 40Hz MQTT.
        # Still detect real shakes; rest/noise stays "equal" so heartbeat carries motion.
        for k, e in (
            ("gyro_x", 25.0),
            ("gyro_y", 25.0),
            ("gyro_z", 25.0),
            ("accel_x", 0.15),
            ("accel_y", 0.15),
            ("accel_z", 0.15),
        ):
            if abs(float(am.get(k, 0)) - float(bm.get(k, 0))) > e:
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
        snap["touchpad"] = {
            "active": False,
            "x": 0.0,
            "y": 0.0,
            "fingers": 0,
            "contacts": [],
        }
    return snap


def public_snapshot(snap: dict) -> dict:
    """Strip internal/debug keys before MQTT publish."""
    allowed = {"connected", "source", "axes", "buttons", "touchpad", "motion", "ts"}
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
    """Open DS4 via hidapi. Returns (dev, info) or (None, None)."""
    from deep_dog_ds4_hid import open_ds4_device

    return open_ds4_device(hid_mod, path=path)


def _parse_output_rumble_led(obj: dict):
    """Parse I09 output fields → (weak, strong, duration_ms, r, g, b)."""
    led = obj.get("led") if isinstance(obj.get("led"), dict) else None
    rumble = obj.get("rumble") if isinstance(obj.get("rumble"), dict) else None
    r = g = b = None
    if led is not None:
        if "r" in led:
            r = int(led.get("r", 0))
        if "g" in led:
            g = int(led.get("g", 0))
        if "b" in led:
            b = int(led.get("b", 0))
    weak = float(rumble.get("weak", 0) if rumble else 0)
    strong = float(rumble.get("strong", 0) if rumble else 0)
    weak = clamp(weak, 0.0, 1.0)
    strong = clamp(strong, 0.0, 1.0)
    duration_ms = obj.get("duration_ms")
    try:
        duration_ms = int(duration_ms) if duration_ms is not None else None
    except (TypeError, ValueError):
        duration_ms = None
    if duration_ms is not None:
        duration_ms = max(0, min(5000, duration_ms))
    return weak, strong, duration_ms, r, g, b


class PadOutputCtl:
    """Thread-safe handle/cmd action=output: ds4_hid (灯+震) or pygame rumble (Xbox/通用)."""

    def __init__(self, backend: str = "ds4_hid") -> None:
        assert backend in ("ds4_hid", "pygame")
        self.backend = backend
        self._lock = threading.Lock()
        self._dev = None  # DS4 hid device
        self._joy = None  # pygame Joystick
        self._timer: threading.Timer | None = None

    def set_dev(self, dev) -> None:
        with self._lock:
            self._dev = dev

    def set_joy(self, joy) -> None:
        with self._lock:
            self._joy = joy

    def _cancel_timer_locked(self) -> None:
        if self._timer is not None:
            self._timer.cancel()
            self._timer = None

    def clear_rumble(self) -> None:
        with self._lock:
            self._cancel_timer_locked()
            backend = self.backend
            dev = self._dev
            joy = self._joy
        if backend == "ds4_hid":
            if dev is None:
                return
            from deep_dog_ds4_hid import apply_ds4_output

            apply_ds4_output(dev, rumble_weak=0.0, rumble_strong=0.0)
            return
        if joy is None:
            return
        try:
            if hasattr(joy, "stop_rumble"):
                joy.stop_rumble()
            elif hasattr(joy, "rumble"):
                joy.rumble(0.0, 0.0, 1)
        except Exception:
            pass

    def apply_output_cmd(self, obj: dict) -> bool:
        weak, strong, duration_ms, r, g, b = _parse_output_rumble_led(obj)

        with self._lock:
            self._cancel_timer_locked()
            backend = self.backend
            dev = self._dev
            joy = self._joy

        if backend == "ds4_hid":
            if dev is None:
                return False
            from deep_dog_ds4_hid import apply_ds4_output

            ok = apply_ds4_output(
                dev,
                rumble_weak=weak,
                rumble_strong=strong,
                r=r,
                g=g,
                b=b,
            )
            if ok and duration_ms and (weak > 0 or strong > 0):
                with self._lock:
                    t = threading.Timer(duration_ms / 1000.0, self.clear_rumble)
                    t.daemon = True
                    self._timer = t
                    t.start()
            return ok

        # pygame: led ignored; strong→low-freq, weak→high-freq (I09)
        if joy is None or not hasattr(joy, "rumble"):
            return False
        try:
            if strong <= 0 and weak <= 0:
                if hasattr(joy, "stop_rumble"):
                    joy.stop_rumble()
                else:
                    joy.rumble(0.0, 0.0, 1)
                return True
            dur = duration_ms if duration_ms and duration_ms > 0 else 250
            joy.rumble(strong, weak, dur)
            return True
        except Exception as e:
            print(f"[{ts()}] pygame rumble failed: {e}", file=sys.stderr)
            return False


# Back-compat alias (defaults to ds4_hid)
Ds4OutputCtl = PadOutputCtl


def probe_ds4_output() -> int:
    """Local LED/rumble smoke test without MQTT."""
    from deep_dog_ds4_hid import apply_ds4_output

    hid_mod = try_import_hid()
    if hid_mod is None:
        print("pip3 install hidapi", file=sys.stderr)
        return 2
    dev, info = acquire_ds4_hid(hid_mod)
    if dev is None:
        print("no DS4 found", file=sys.stderr)
        return 1
    print(f"[{ts()}] probe-output product={info.get('product_string')!r}")
    try:
        apply_ds4_output(dev, rumble_weak=0.0, rumble_strong=0.0, r=0, g=80, b=255)
        time.sleep(0.4)
        apply_ds4_output(dev, rumble_weak=0.3, rumble_strong=0.5, r=200, g=40, b=0)
        time.sleep(0.5)
        apply_ds4_output(dev, rumble_weak=0.0, rumble_strong=0.0, r=0, g=0, b=64)
        print(f"[{ts()}] probe-output done")
        return 0
    finally:
        try:
            apply_ds4_output(dev, rumble_weak=0.0, rumble_strong=0.0)
            dev.close()
        except Exception:
            pass


def probe_xbox_rumble() -> int:
    """Local Xbox/pygame rumble smoke test without MQTT (prefer BT on macOS)."""
    require_pygame()
    pygame.init()
    pygame.joystick.init()
    joy, layout = acquire_joystick(0, "auto")
    if joy is None:
        print("no joystick found (plug Xbox; macOS prefer Bluetooth)", file=sys.stderr)
        return 1
    name = joy.get_name()
    print(f"[{ts()}] probe-xbox-rumble name={name!r} layout={layout}")
    if not hasattr(joy, "rumble"):
        print("joystick.rumble unsupported (pygame/SDL too old?)", file=sys.stderr)
        return 1
    ctl = PadOutputCtl(backend="pygame")
    ctl.set_joy(joy)
    try:
        ok1 = ctl.apply_output_cmd(
            {"action": "output", "rumble": {"strong": 0.5, "weak": 0.3}, "duration_ms": 400}
        )
        time.sleep(0.5)
        ok2 = ctl.apply_output_cmd({"action": "output", "rumble": {"strong": 0, "weak": 0}})
        print(f"[{ts()}] probe-xbox-rumble done ok_on={ok1} ok_off={ok2}")
        return 0 if ok1 else 1
    finally:
        ctl.clear_rumble()
        pygame.quit()


def read_hid_snapshot(dev) -> dict | None:
    from deep_dog_ds4_hid import parse_ds4_usb_report, read_ds4_report

    # OSError propagates: bridge treats it as disconnect and rescans.
    data = read_ds4_report(dev, wait_ms=20.0, allow_short=True)
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
    output_ctl: "Ds4OutputCtl | None" = None,
) -> int:
    """HID-only bridge path: buttons + axes + touchpad + motion; optional output."""
    from deep_dog_ds4_hid import wake_ds4_full_reports

    hid_mod = try_import_hid()
    if hid_mod is None:
        print(
            "missing dependency for HID mode: pip3 install hidapi "
            f"(interpreter={sys.executable})",
            file=sys.stderr,
        )
        return 2

    path = hid_path.encode("utf-8") if hid_path else None
    interval = 1.0 / max(1.0, hz)
    heartbeat_s = max(0.05, heartbeat_ms / 1000.0)
    reconnect_s = max(0.2, reconnect_ms / 1000.0)
    # Wall-clock disconnect: empty nonblocking reads are normal between reports.
    stale_s = 2.0

    def bind_dev(d):
        if output_ctl is not None:
            output_ctl.set_dev(d)

    dev, info = acquire_ds4_hid(hid_mod, path=path)
    bind_dev(dev)
    if dev is None:
        if not wait_pad:
            print(
                "no DS4 found for HID mode; plug USB DualShock 4 "
                "(or pass --wait-pad / --no-touchpad-xy)",
                file=sys.stderr,
            )
            return 1
        print(f"[{ts()}] HID mode: no DS4 yet; waiting…")

    last_snap = None
    last_pub = 0.0
    last_rescan = 0.0
    last_good = 0.0
    last_wake = 0.0
    pad_online = False
    warned_short = False

    if info is not None:
        pid = int(info.get("product_id") or 0)
        print(
            f"[{ts()}] HID mode pid=0x{pid:04X} "
            f"product={info.get('product_string')!r}"
        )
        pad_online = True
        last_good = time.time()

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
                        bind_dev(None)
                    dev, info = acquire_ds4_hid(hid_mod, path=path)
                    bind_dev(dev)
                    if dev is not None:
                        print(f"[{ts()}] DS4 reconnected (HID)")
                        pad_online = True
                        last_snap = None
                        last_good = now
                        warned_short = False
                time.sleep(0.05)
                continue

            try:
                snap = read_hid_snapshot(dev)
            except OSError as e:
                print(f"[{ts()}] HID read error ({e}); will rescan")
                last_good = now - stale_s
                snap = None
            if snap is None:
                # Distinguish idle gap vs hard IO failure: if last_good is fresh,
                # just wait; after stale_s, close and rescan (also covers OSError).
                if last_good > 0 and (now - last_good) >= stale_s:
                    print(f"[{ts()}] HID stale — publish connected=false; will rescan")
                    pad_online = False
                    off = offline_snapshot(with_touchpad=True)
                    publish_snap(client, input_topic, off)
                    last_pub = now
                    last_snap = off
                    try:
                        if dev is not None:
                            dev.close()
                    except Exception:
                        pass
                    dev = None
                    bind_dev(None)
                elif now - last_wake >= 0.5:
                    last_wake = now
                    try:
                        if dev is not None:
                            wake_ds4_full_reports(dev, force=True)
                    except Exception:
                        # write/read failure → force rescan next stale window
                        last_good = min(last_good, now - stale_s) if last_good else now - stale_s
                time.sleep(0.002)
                continue

            last_good = now
            report_len = int(snap.get("_len") or 0)
            if report_len and report_len < 64 and not warned_short:
                warned_short = True
                print(
                    f"[{ts()}] HID short reports ({report_len}B) — "
                    "axes/buttons ok; touchpad/motion may be unavailable"
                )
            elif report_len >= 64 and warned_short:
                warned_short = False
                print(f"[{ts()}] HID full reports restored ({report_len}B)")

            pad_online = True
            changed = not snapshot_equal(snap, last_snap)
            due_change = changed and (now - last_pub >= interval)
            due_heartbeat = (now - last_pub) >= heartbeat_s
            if due_change or due_heartbeat:
                snap = {**snap, "ts": int(now)}
                publish_snap(client, input_topic, snap)
                last_pub = now
                last_snap = snap
            if now - last_wake >= 1.0:
                last_wake = now
                try:
                    wake_ds4_full_reports(dev)
                except Exception:
                    pass
            time.sleep(0.001)
    except KeyboardInterrupt:
        print(f"[{ts()}] stopping; publish connected=false")
        publish_snap(client, input_topic, offline_snapshot(with_touchpad=True))
        time.sleep(0.2)
    finally:
        if output_ctl is not None:
            output_ctl.clear_rumble()
            output_ctl.set_dev(None)
        if dev is not None:
            try:
                dev.close()
            except Exception:
                pass
    return 0


def probe_joystick(index: int, seconds: float = 20.0) -> int:
    """Print pygame raw axes/buttons + interpret under each layout (真源对照，勿盲信映射)。"""
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
    # Warm up HID so triggers leave 0 → -1 on macOS
    for _ in range(10):
        pygame.event.pump()
        time.sleep(0.02)
    idle = [round(joy.get_axis(i), 3) for i in range(joy.get_numaxes())]
    print(f"RAW IDLE axes: {idle}")
    if joy.get_numhats():
        print(f"RAW IDLE hat0: {joy.get_hat(0)}")
    print("Interpreted @idle (抽象契约；选与全零 sticks + l2/r2≈0 最接近的 layout):")
    for name in ("xbox_sdl", "xbox_xinput", "ds4_sdl", "ds4_linux"):
        snap = read_snapshot(joy, name)
        ax, bt = snap["axes"], snap["buttons"]
        print(
            f"  {name:12} lx={ax['lx']:+.2f} ly={ax['ly']:+.2f} "
            f"rx={ax['rx']:+.2f} ry={ax['ry']:+.2f} "
            f"l2={float(bt['l2']):.2f} r2={float(bt['r2']):.2f}"
        )
    print("Press buttons / move sticks… (lines are RAW pygame indices)")
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
        help="profile；xbox/xbox_sdl/xbox_xinput 默认走 pygame（等同 --no-touchpad-xy）",
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
        dest="touchpad_xy",
        action="store_true",
        default=True,
        help="DS4 hidapi full-read + handle/cmd output (default on)",
    )
    ap.add_argument(
        "--no-touchpad-xy",
        dest="touchpad_xy",
        action="store_false",
        help="pygame path (Xbox/通用输入 + handle/cmd output 震动；无触控/灯条)",
    )
    ap.add_argument(
        "--hid-path",
        default=None,
        help="optional hidapi path for HID mode (from probe --list)",
    )
    ap.add_argument(
        "--probe-output",
        action="store_true",
        help="local DS4 LED/rumble smoke test then exit (no MQTT)",
    )
    ap.add_argument(
        "--probe-xbox-rumble",
        action="store_true",
        help="local Xbox/pygame rumble smoke test then exit (no MQTT; prefer BT on macOS)",
    )
    args = ap.parse_args()

    # Xbox 无触控板/灯条：选 xbox* 时默认 pygame（不必再写 --no-touchpad-xy）
    if args.layout in ("xbox", "xbox_sdl", "xbox_xinput") and args.touchpad_xy:
        args.touchpad_xy = False

    if args.probe_output:
        return probe_ds4_output()
    if args.probe_xbox_rumble:
        return probe_xbox_rumble()

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
    cmd_topic = f"{prefix}/handle/cmd"

    use_ws = args.via == "web"
    transport = "websockets" if use_ws else "tcp"
    client_id = f"deep-dog-handle-bridge-{int(time.time())}"
    connected = {"ok": False}
    if args.touchpad_xy:
        output_ctl: PadOutputCtl | None = PadOutputCtl(backend="ds4_hid")
    else:
        output_ctl = PadOutputCtl(backend="pygame")
        if joy is not None:
            output_ctl.set_joy(joy)

    def on_connect(client, userdata, flags, reason_code, properties=None):
        rc = reason_code if isinstance(reason_code, int) else getattr(reason_code, "value", reason_code)
        if rc != 0:
            print(f"[{ts()}] CONNECT failed rc={rc}", file=sys.stderr)
            return
        connected["ok"] = True
        client.subscribe(status_topic, qos=0)
        if output_ctl is not None:
            client.subscribe(cmd_topic, qos=1)
            print(
                f"[{ts()}] CONNECTED; PUB {input_topic}; "
                f"SUB {status_topic} + {cmd_topic} (output backend={output_ctl.backend})"
            )
        else:
            print(f"[{ts()}] CONNECTED; PUB {input_topic}; SUB {status_topic}")

    def on_message(client, userdata, msg):
        payload = msg.payload.decode("utf-8", errors="replace")
        topic = msg.topic
        if output_ctl is not None and topic == cmd_topic:
            try:
                o = json.loads(payload)
            except Exception:
                print(f"[{ts()}] CMD bad json | {payload[:120]}")
                return
            if o.get("action") != "output":
                return
            ok = output_ctl.apply_output_cmd(o)
            led = o.get("led") or {}
            rum = o.get("rumble") or {}
            backend = output_ctl.backend
            bus = backend
            if backend == "ds4_hid":
                try:
                    from deep_dog_ds4_hid import _ds4_bus_type

                    bus = "bt" if int(_ds4_bus_type) == 2 else f"bus={_ds4_bus_type}"
                except Exception:
                    bus = "ds4_hid"
            print(
                f"[{ts()}] CMD output ok={1 if ok else 0} backend={backend} bus={bus} "
                f"led={led} rumble={rum} duration_ms={o.get('duration_ms')}"
            )
            return
        summary = ""
        try:
            o = json.loads(payload)
            tp = o.get("touchpad")
            bt = (o.get("buttons") or {}).get("touch")
            ax = o.get("axes") or {}
            if isinstance(tp, dict):
                n_c = len(tp.get("contacts") or []) if isinstance(tp.get("contacts"), list) else 0
                summary = (
                    f" touchpad={{active:{1 if tp.get('active') else 0} "
                    f"x:{float(tp.get('x', 0)):.2f} y:{float(tp.get('y', 0)):.2f} "
                    f"fingers:{int(tp.get('fingers', 0))} contacts:{n_c}}}"
                )
            else:
                summary = " touchpad=<absent>"
            mo = o.get("motion")
            if isinstance(mo, dict):
                summary += (
                    f" motion={{gx:{float(mo.get('gyro_x', 0)):.1f} "
                    f"gy:{float(mo.get('gyro_y', 0)):.1f} "
                    f"gz:{float(mo.get('gyro_z', 0)):.1f} "
                    f"ax:{float(mo.get('accel_x', 0)):.2f} "
                    f"ay:{float(mo.get('accel_y', 0)):.2f} "
                    f"az:{float(mo.get('accel_z', 0)):.2f}}}"
                )
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
            f"[{ts()}] HID full-read + cmd/output feedback; "
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
                output_ctl=output_ctl,
            )
        finally:
            client.loop_stop()
            client.disconnect()

    print(
        f"[{ts()}] pygame path (--no-touchpad-xy); "
        f"handle/cmd output rumble enabled (Xbox/pygame; led ignored)"
    )
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
    if output_ctl is not None and joy is not None:
        output_ctl.set_joy(joy)
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
                        if output_ctl is not None:
                            output_ctl.clear_rumble()
                            output_ctl.set_joy(None)
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
                        if output_ctl is not None:
                            output_ctl.set_joy(joy)

            if not joystick_alive(joy):
                if pad_online:
                    print(f"[{ts()}] pad lost/stale — publish connected=false; will rescan")
                    pad_online = False
                    need_hard_rescan = True
                    if output_ctl is not None:
                        output_ctl.clear_rumble()
                        output_ctl.set_joy(None)
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
                        if output_ctl is not None:
                            output_ctl.set_joy(joy)
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
        if output_ctl is not None:
            output_ctl.clear_rumble()
        publish_snap(client, input_topic, offline_snapshot())
        time.sleep(0.2)
    finally:
        if output_ctl is not None:
            output_ctl.clear_rumble()
            output_ctl.set_joy(None)
        client.loop_stop()
        client.disconnect()
        pygame.quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
