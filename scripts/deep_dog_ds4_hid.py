#!/usr/bin/env python3
"""Shared DualShock 4 HID report parser for deep-dog (I06 touchpad + I07 motion).

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


def trigger_deadzone(v: float, dz: float = 0.12) -> float:
    return 0.0 if v < dz else v


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


def _i16le(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little", signed=True)


def parse_motion(payload: bytes, gyro_off: int) -> dict | None:
    """DS4 IMU → deep-dog body frame. Units: dps (/16), g (/8192).

    HID report order is sensor-native. Accel remaps to body frame
    (+X right, +Y forward, +Z up):

        a_body = R @ a_hid
        R: (x, y, z) → (-x, z, -y)   # det(R) = -1

    Flat face-up → accel_z ≈ -1 g; right side down → accel_x ≈ +1 g.

    Gyro is an axial vector: under improper R, ω_body = det(R) * R @ ω_hid
    so that the body frame stays right-handed (I07).
    """
    if gyro_off < 0 or len(payload) < gyro_off + 12:
        return None
    gx = _i16le(payload, gyro_off)
    gy = _i16le(payload, gyro_off + 2)
    gz = _i16le(payload, gyro_off + 4)
    ax = _i16le(payload, gyro_off + 6)
    ay = _i16le(payload, gyro_off + 8)
    az = _i16le(payload, gyro_off + 10)
    # Accel: R @ a = (-x, z, -y)
    # Gyro:  det(R) R @ ω = -(-x, z, -y) = (x, -z, y)
    return {
        "gyro_x": round(gx / 16.0, 3),
        "gyro_y": round(-gz / 16.0, 3),
        "gyro_z": round(gy / 16.0, 3),
        "accel_x": round(-ax / 8192.0, 4),
        "accel_y": round(az / 8192.0, 4),
        "accel_z": round(-ay / 8192.0, 4),
    }


def _norm_tp(raw_x: int, raw_y: int) -> tuple[float, float]:
    return (
        clamp(raw_x / float(TP_X_MAX), 0.0, 1.0),
        clamp(raw_y / float(TP_Y_MAX), 0.0, 1.0),
    )


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
    """Parse DS4 report → abstract handle snapshot (+ touchpad contacts + motion).

    Full reports (USB 0x01 / BT 0x11) share Linux ``dualshock4_input_report_common``:

        x,y,rx,ry | buttons[3] | z,rz(L2/R2) | timestamp | temp | gyro[3] | accel[3] | …

    - USB: strip 1-byte report-id → common at payload[0]
    - BT:  strip 0x11 + 2 reserved → common at payload[0]
    - gyro starts at payload offset **12** (NOT 16 — 16 was mis-aligned and
      produced gz≈500 garbage, flooding MQTT via motion on-change)

    Short macOS GamePad frames (~10B, still report-id 0x01) are sticks-only
    best-effort; no reliable IMU/touch.

    Stick polarity: right/down positive (I01).
    Touchpad: x left→right, y top→bottom, [0,1]; contacts up to 2 (I06).
    Motion: gyro dps, accel g (I07).
    """
    if not data or len(data) < 10:
        return None

    report_id = data[0]
    full = len(data) >= 64
    # Common-struct offsets (full report). Short GamePad keeps legacy trig@4.
    if report_id == 0x11:
        if len(data) < 12:
            return None
        payload = data[3:]
        touch_default = 33  # num_touch_reports @32, first finger @33
    elif report_id == 0x01:
        payload = data[1:]
        touch_default = 34
    elif report_id in (0x00,) or report_id > 0x20:
        payload = data
        report_id = 0
        touch_default = 34
    else:
        payload = data[1:]
        touch_default = 34

    if full:
        btn_i, trig_i, gyro_off = 4, 7, 12
    else:
        # macOS 10B: sticks + hat-ish; treat like old USB packing
        btn_i, trig_i, gyro_off = 6, 4, -1

    if len(payload) < max(btn_i + 3, trig_i + 2, 9):
        return None

    lx = axis_deadzone(u8_to_axis(payload[0]))
    # DS4 HID Y：0=物理上推；抽象约定「下为正 / 前推 ly<0」与 pygame ds4_sdl 一致，垂直取反
    ly = axis_deadzone(-u8_to_axis(payload[1]))
    rx = axis_deadzone(u8_to_axis(payload[2]))
    ry = axis_deadzone(-u8_to_axis(payload[3]))
    # macOS 10B GamePad often has L2 idle noise (~0x08); deadzone short frames more.
    trig_dz = 0.12 if not full else 0.04
    l2 = trigger_deadzone(u8_to_trigger(payload[trig_i]), trig_dz)
    r2 = trigger_deadzone(u8_to_trigger(payload[trig_i + 1]), trig_dz)

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
    contacts: list[dict] = []
    if touch_off is not None and touch_off + 8 < len(payload):
        a0, x0, y0 = parse_touch_finger(payload, touch_off + 1)
        a1, x1, y1 = parse_touch_finger(payload, touch_off + 5)
        for a, x, y in ((a0, x0, y0), (a1, x1, y1)):
            ok = bool(a and _finger_coords_ok(True, x, y))
            nx, ny = _norm_tp(x, y) if ok else (0.0, 0.0)
            contacts.append({"active": ok, "x": round(nx, 4), "y": round(ny, 4)})
            if ok:
                fingers += 1
                if not active:
                    active = True
                    tx, ty = nx, ny

    motion = parse_motion(payload, gyro_off) if gyro_off >= 0 else None

    out: dict = {
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
            "contacts": contacts,
        },
        "ts": int(time.time()),
        "_touch_off": touch_off,
        "_len": len(data),
        "_pay_len": len(payload),
    }
    if motion is not None:
        out["motion"] = motion
    return out


_last_ds4_wake = 0.0

# Last applied lightbar / motors (sparse MQTT + wake must not clobber)
_ds4_led_rgb = (0, 0, 64)
_ds4_rumble = (0.0, 0.0)  # weak, strong


def clamp_u8(v: float | int) -> int:
    iv = int(round(float(v)))
    return 0 if iv < 0 else 255 if iv > 255 else iv


def build_ds4_output_report(
    *,
    rumble_weak: float = 0.0,
    rumble_strong: float = 0.0,
    r: int = 0,
    g: int = 0,
    b: int = 64,
    flash_on: int = 0,
    flash_off: int = 0,
) -> bytes:
    """DS4 USB/BT output report 0x05 (32 bytes incl. report id).

    Byte layout (common community map):
      [0]=0x05 [1]=0xFF [2]=0x04 [3]=0x00
      [4]=weak(right/small) [5]=strong(left/large) 0..255
      [6]=R [7]=G [8]=B
      [9]=flash_on [10]=flash_off (0=steady)
    """
    weak = clamp_u8(max(0.0, min(1.0, float(rumble_weak))) * 255.0)
    strong = clamp_u8(max(0.0, min(1.0, float(rumble_strong))) * 255.0)
    pkt = bytearray(32)
    pkt[0] = 0x05
    pkt[1] = 0xFF
    pkt[2] = 0x04
    pkt[3] = 0x00
    pkt[4] = weak
    pkt[5] = strong
    pkt[6] = clamp_u8(r)
    pkt[7] = clamp_u8(g)
    pkt[8] = clamp_u8(b)
    pkt[9] = clamp_u8(flash_on)
    pkt[10] = clamp_u8(flash_off)
    return bytes(pkt)


def apply_ds4_output(
    dev,
    *,
    rumble_weak: float = 0.0,
    rumble_strong: float = 0.0,
    r: int | None = None,
    g: int | None = None,
    b: int | None = None,
    flash_on: int = 0,
    flash_off: int = 0,
    remember_led: bool = True,
) -> bool:
    """Write lightbar + rumble. None led channels keep last remembered color."""
    global _ds4_led_rgb, _ds4_rumble
    lr, lg, lb = _ds4_led_rgb
    rr = lr if r is None else clamp_u8(r)
    gg = lg if g is None else clamp_u8(g)
    bb = lb if b is None else clamp_u8(b)
    if remember_led:
        _ds4_led_rgb = (rr, gg, bb)
    _ds4_rumble = (
        max(0.0, min(1.0, float(rumble_weak))),
        max(0.0, min(1.0, float(rumble_strong))),
    )
    pkt = build_ds4_output_report(
        rumble_weak=_ds4_rumble[0],
        rumble_strong=_ds4_rumble[1],
        r=rr,
        g=gg,
        b=bb,
        flash_on=flash_on,
        flash_off=flash_off,
    )
    try:
        dev.write(pkt)
        return True
    except Exception:
        return False


def wake_ds4_full_reports(dev, *, force: bool = False) -> None:
    """Ask DS4 for full HID stream (64/78B with touchpad), not macOS 10B GamePad."""
    global _last_ds4_wake
    now = time.time()
    if not force and (now - _last_ds4_wake) < 0.5:
        return
    _last_ds4_wake = now
    # Keep current rumble/LED while refreshing report mode (do not zero motors).
    apply_ds4_output(
        dev,
        rumble_weak=_ds4_rumble[0],
        rumble_strong=_ds4_rumble[1],
        remember_led=True,
    )
    # Feature 0x02 (calibration) / 0x12 often flips macOS BT from 10B → 0x11 78B.
    for rid, size in ((0x02, 37), (0x12, 16)):
        try:
            dev.get_feature_report(rid, size)
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
    # Give the stack a moment to switch report mode, then confirm full size.
    time.sleep(0.05)
    try:
        for _ in range(30):
            chunk = dev.read(78)
            if not chunk:
                break
            if len(chunk) >= 64:
                wake_ds4_full_reports(dev, force=True)
                break
            wake_ds4_full_reports(dev)
    except Exception:
        pass
    time.sleep(0.02)
    return dev, info


def read_ds4_report(
    dev, *, min_len: int = 64, wait_ms: float = 25.0, allow_short: bool = True
) -> bytes | None:
    """Read one input report (prefer USB~64 / BT~78; optional 10B GamePad fallback).

    Non-blocking empty reads are retried until wait_ms — do NOT treat the first
    empty as disconnect (that caused reconnect storms on macOS BT).
    Raises OSError only if the caller wants to handle device loss; transient
    read errors return None so the bridge can soft-reconnect.
    """
    deadline = time.time() + max(0.0, wait_ms) / 1000.0
    best_short: bytes | None = None
    while True:
        try:
            data = dev.read(78)
        except OSError:
            # macOS BT often surfaces disconnect / IO failure as read error
            raise
        if data:
            data = bytes(data)
            if len(data) >= min_len:
                return data
            if len(data) >= 10:
                best_short = data
                wake_ds4_full_reports(dev)
        if time.time() >= deadline:
            break
        time.sleep(0.001)
    if allow_short and best_short is not None:
        return best_short
    return None
