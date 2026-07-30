#!/usr/bin/env python3
"""Shared DualShock 4 HID report parser for deep-dog (I06).

Used by deep_dog_ds4_touchpad_probe.py and deep_dog_handle_bridge.py --touchpad-xy.
"""

from __future__ import annotations

import time

SONY_VID = 0x054C
DS4_PIDS = (0x05C4, 0x09CC, 0x0BA0)  # DS4 v1 / v2 / USB wireless adapter

TP_X_MAX = 1919
TP_Y_MAX = 941


def clamp(v: float, lo: float, hi: float) -> float:
    return lo if v < lo else hi if v > hi else v


def axis_deadzone(v: float, dz: float = 0.08) -> float:
    return 0.0 if abs(v) < dz else clamp(v, -1.0, 1.0)


def u8_to_axis(b: int) -> float:
    return clamp((b - 128) / 128.0, -1.0, 1.0)


def u8_to_trigger(b: int) -> float:
    return clamp(b / 255.0, 0.0, 1.0)


def is_ds4(d: dict) -> bool:
    return int(d.get("product_id") or 0) in DS4_PIDS


def list_sony_devices(hid) -> list[dict]:
    return list(hid.enumerate(SONY_VID))


def pick_ds4_device(hid, path: bytes | None = None) -> dict | None:
    devices = list_sony_devices(hid)
    ds4 = [d for d in devices if is_ds4(d)]
    if path is not None:
        for d in ds4:
            if d.get("path") == path:
                return d
        return None
    for d in ds4:
        usage_page = int(d.get("usage_page") or 0)
        usage = int(d.get("usage") or 0)
        if usage_page == 0x01 and usage in (0x04, 0x05):
            return d
    return ds4[0] if ds4 else None


def parse_touch_finger(data: bytes, offset: int) -> tuple[bool, int, int]:
    """Return (active, raw_x, raw_y). tracking bit7=1 → not touching."""
    if offset + 3 >= len(data):
        return False, 0, 0
    tracking = data[offset]
    active = (tracking & 0x80) == 0
    raw_x = data[offset + 1] | ((data[offset + 2] & 0x0F) << 8)
    raw_y = ((data[offset + 2] & 0xF0) >> 4) | (data[offset + 3] << 4)
    return active, raw_x, raw_y


def _finger_coords_ok(active: bool, x: int, y: int) -> bool:
    """Inactive DS4 fingers often sit at 0x7FF sentinel — do not reject those."""
    if not active:
        return True
    return x <= TP_X_MAX + 40 and y <= TP_Y_MAX + 40


def pick_touch_counter_offset(
    payload: bytes, *, default: int | None = 34
) -> int | None:
    """Touch counter inside payload (report-id stripped).

    USB (payload=data[1:]): counter @34 (abs 35).
    macOS BT (payload=data[3:]): counter @33 (abs 36) — finger0 @34.
    """
    for cand in (33, 34, 35, 32, 36):
        if cand + 8 >= len(payload):
            continue
        a0, x0, y0 = parse_touch_finger(payload, cand + 1)
        a1, x1, y1 = parse_touch_finger(payload, cand + 5)
        if _finger_coords_ok(a0, x0, y0) and _finger_coords_ok(a1, x1, y1):
            return cand
    if default is not None and len(payload) >= (default + 9):
        return default
    return 34 if len(payload) >= 43 else None


def parse_ds4_usb_report(data: bytes) -> dict | None:
    """Parse DS4 report → abstract handle snapshot (+ touchpad).

    USB (report 0x01, ~64B): [1]LX..[4]RY [5]L2 [6]R2 [7..9]buttons; touch @35
    BT  (report 0x11, ~78B): skip 0x11 0xC0 0x00 → sticks; buttons then L2/R2;
                             touch counter @36 (payload@33).

    macOS may briefly emit 10B GamePad reports — caller should drop those
    (see read_ds4_report); they have no touchpad and mis-map L2.

    Stick polarity: right/down positive (I01).
    Touchpad: x left→right, y top→bottom, [0,1].
    """
    if not data or len(data) < 10:
        return None

    report_id = data[0]
    touch_default = 34
    if report_id == 0x01:
        # USB: strip report-id only
        payload = data[1:]
        btn_i, trig_i = 6, 4
        touch_default = 34
    elif report_id == 0x11:
        # BT: strip report-id + 0xC0 + 0x00; buttons precede analog triggers
        if len(data) < 12:
            return None
        payload = data[3:]
        btn_i, trig_i = 4, 7
        touch_default = 33
    elif report_id in (0x00,) or report_id > 0x20:
        # Some stacks omit report-id; treat as USB payload
        payload = data
        report_id = 0
        btn_i, trig_i = 6, 4
    else:
        payload = data[1:]
        btn_i, trig_i = 6, 4

    if len(payload) < max(btn_i + 3, trig_i + 2, 9):
        return None

    lx = axis_deadzone(u8_to_axis(payload[0]))
    ly = axis_deadzone(u8_to_axis(payload[1]))
    rx = axis_deadzone(u8_to_axis(payload[2]))
    ry = axis_deadzone(u8_to_axis(payload[3]))
    l2 = u8_to_trigger(payload[trig_i])
    r2 = u8_to_trigger(payload[trig_i + 1])

    buttons1 = payload[btn_i]
    buttons2 = payload[btn_i + 1]
    buttons3 = payload[btn_i + 2]

    hat = buttons1 & 0x0F
    square = bool(buttons1 & 0x10)
    cross = bool(buttons1 & 0x20)
    circle = bool(buttons1 & 0x40)
    triangle = bool(buttons1 & 0x80)

    l1 = bool(buttons2 & 0x01)
    r1 = bool(buttons2 & 0x02)
    share = bool(buttons2 & 0x10)
    options = bool(buttons2 & 0x20)
    l3 = bool(buttons2 & 0x40)
    r3 = bool(buttons2 & 0x80)

    ps = bool(buttons3 & 0x01)
    touch_click = bool(buttons3 & 0x02)

    if hat == 8:
        dpad_up = dpad_down = dpad_left = dpad_right = False
    else:
        dpad_up = hat in (0, 1, 7)
        dpad_right = hat in (1, 2, 3)
        dpad_down = hat in (3, 4, 5)
        dpad_left = hat in (5, 6, 7)

    touch_off = pick_touch_counter_offset(payload, default=touch_default)
    fingers = 0
    tx = ty = 0.0
    active = False
    if touch_off is not None and touch_off + 8 < len(payload):
        a0, x0, y0 = parse_touch_finger(payload, touch_off + 1)
        a1, x1, y1 = parse_touch_finger(payload, touch_off + 5)
        if a0 and _finger_coords_ok(True, x0, y0):
            fingers += 1
            active = True
            tx = clamp(x0 / float(TP_X_MAX), 0.0, 1.0)
            ty = clamp(y0 / float(TP_Y_MAX), 0.0, 1.0)
        if a1 and _finger_coords_ok(True, x1, y1):
            fingers += 1
            active = True
            if not a0:
                tx = clamp(x1 / float(TP_X_MAX), 0.0, 1.0)
                ty = clamp(y1 / float(TP_Y_MAX), 0.0, 1.0)

    return {
        "connected": True,
        "source": "wifi",
        "report_id": report_id,
        "axes": {"lx": lx, "ly": ly, "rx": rx, "ry": ry},
        "buttons": {
            "a": cross,
            "b": circle,
            "x": square,
            "y": triangle,
            "l1": l1,
            "r1": r1,
            "l2": l2,
            "r2": r2,
            "select": share,
            "start": options,
            "ps": ps,
            "l3": l3,
            "r3": r3,
            "touch": touch_click,
            "dpad_up": dpad_up,
            "dpad_down": dpad_down,
            "dpad_left": dpad_left,
            "dpad_right": dpad_right,
        },
        "touchpad": {
            "active": active,
            "x": round(tx, 4),
            "y": round(ty, 4),
            "fingers": fingers,
        },
        "ts": int(time.time()),
        "_touch_off": touch_off,
        "_len": len(data),
        "_pay_len": len(payload),
    }


_last_ds4_wake = 0.0


def wake_ds4_full_reports(dev, *, force: bool = False) -> None:
    """Ask DS4 for full HID stream (64/78B with touchpad), not macOS 10B GamePad."""
    global _last_ds4_wake
    now = time.time()
    if not force and (now - _last_ds4_wake) < 0.5:
        return
    _last_ds4_wake = now
    # 32-byte 0x05 output is required on macOS BT; shorter writes often leave
    # the stack on Apple's stripped 10-byte reports (no touchpad, L2 noise).
    pkt = bytes([0x05, 0xFF, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00] + [0] * 23)
    try:
        dev.write(pkt)
    except Exception:
        pass


def open_ds4_device(hid, path: bytes | None = None):
    """Open DS4 via hidapi. Returns (device, info) or (None, None)."""
    info = pick_ds4_device(hid, path=path)
    if not info:
        return None, None
    dev = hid.device()
    try:
        dev.open_path(info["path"])
    except Exception:
        return None, None
    try:
        dev.set_nonblocking(True)
    except Exception:
        pass
    wake_ds4_full_reports(dev, force=True)
    # Drain any leftover short reports after mode switch
    try:
        for _ in range(40):
            chunk = dev.read(78)
            if not chunk:
                break
            if len(chunk) >= 64:
                break
    except Exception:
        pass
    return dev, info


def read_ds4_report(dev, *, min_len: int = 64) -> bytes | None:
    """Read one full input report (USB ~64 or BT ~78). Drops short GamePad frames."""
    # A few quick retries: macOS may interleave 10B GamePad frames after wake.
    for attempt in range(8):
        data = dev.read(78)
        if not data:
            if attempt == 0:
                return None
            time.sleep(0.001)
            continue
        data = bytes(data)
        if len(data) >= min_len:
            return data
        wake_ds4_full_reports(dev)
        time.sleep(0.001)
    return None
