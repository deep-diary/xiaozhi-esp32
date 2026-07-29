#!/usr/bin/env python3
"""PC gamepad → MQTT handle/input bridge for deep-dog.

Reads a local controller (PS4 / Xbox / generic via pygame) and publishes
snapshots to deepdiary/deep-dog/{device_id}/handle/input.

Dependencies:
  pip3 install paho-mqtt pygame

Credentials (do not commit):
  export DEEP_DOG_MQTT_USER=...
  export DEEP_DOG_MQTT_PASS=...

Examples:
  /usr/bin/python3 scripts/deep_dog_handle_bridge.py --via lan
  /usr/bin/python3 scripts/deep_dog_handle_bridge.py --via web --hz 20
  /usr/bin/python3 scripts/deep_dog_handle_bridge.py --list-joysticks
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

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("missing dependency: pip3 install paho-mqtt", file=sys.stderr)
    sys.exit(2)

try:
    import pygame
except ImportError:
    print("missing dependency: pip3 install pygame", file=sys.stderr)
    sys.exit(2)

WEB_WSS_DEFAULT = "wss://mqtt-ws.deep-diary.com/mqtt"
LAN_HOST_DEFAULT = "192.168.31.25"
LAN_PORT_DEFAULT = 1883


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


def read_snapshot(joy: pygame.joystick.Joystick) -> dict:
    """Map pygame joystick to deep-dog handle snapshot (best-effort for PS4/Xbox)."""
    pygame.event.pump()
    n_axes = joy.get_numaxes()
    n_buttons = joy.get_numbuttons()

    def ax(i: int) -> float:
        return axis_deadzone(joy.get_axis(i)) if i < n_axes else 0.0

    def btn(i: int) -> bool:
        return bool(joy.get_button(i)) if i < n_buttons else False

    # Common layouts: 0/1 left stick, 2/3 right stick (Xbox / many DS4 via pygame)
    lx, ly = ax(0), ax(1)
    rx, ry = ax(2), ax(3)
    # Some DS4 expose triggers as axes 4/5
    l2 = clamp((ax(4) + 1.0) * 0.5, 0.0, 1.0) if n_axes > 4 else (1.0 if btn(6) else 0.0)
    r2 = clamp((ax(5) + 1.0) * 0.5, 0.0, 1.0) if n_axes > 5 else (1.0 if btn(7) else 0.0)

    # Button indices vary; these match many Xbox / DualShock pygame mappings
    return {
        "connected": True,
        "source": "wifi",
        "axes": {"lx": lx, "ly": ly, "rx": rx, "ry": ry},
        "buttons": {
            "a": btn(0),
            "b": btn(1),
            "x": btn(2),
            "y": btn(3),
            "l1": btn(4),
            "r1": btn(5),
            "l2": l2,
            "r2": r2,
            "select": btn(8),
            "start": btn(9),
        },
        "ts": int(time.time()),
    }


def snapshot_equal(a: dict | None, b: dict | None, eps: float = 0.04) -> bool:
    if a is None or b is None:
        return False
    if a.get("connected") != b.get("connected"):
        return False
    aa, ba = a.get("axes", {}), b.get("axes", {})
    for k in ("lx", "ly", "rx", "ry"):
        if abs(float(aa.get(k, 0)) - float(ba.get(k, 0))) > eps:
            return False
    ab, bb = a.get("buttons", {}), b.get("buttons", {})
    for k in ("a", "b", "x", "y", "l1", "r1", "start", "select"):
        if bool(ab.get(k)) != bool(bb.get(k)):
            return False
    for k in ("l2", "r2"):
        if abs(float(ab.get(k, 0)) - float(bb.get(k, 0))) > eps:
            return False
    return True


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
    ap.add_argument("--hz", type=float, default=20.0, help="max publish rate")
    ap.add_argument("--list-joysticks", action="store_true")
    args = ap.parse_args()

    pygame.init()
    pygame.joystick.init()
    count = pygame.joystick.get_count()
    if args.list_joysticks:
        print(f"joysticks: {count}")
        for i in range(count):
            j = pygame.joystick.Joystick(i)
            j.init()
            print(f"  [{i}] {j.get_name()} axes={j.get_numaxes()} buttons={j.get_numbuttons()}")
        return 0

    if count <= 0:
        print("no joystick found; plug in PS4/Xbox (USB or OS bluetooth)", file=sys.stderr)
        return 1
    if args.joystick < 0 or args.joystick >= count:
        print(f"invalid --joystick {args.joystick}; have 0..{count - 1}", file=sys.stderr)
        return 1

    joy = pygame.joystick.Joystick(args.joystick)
    joy.init()
    print(f"[{ts()}] using joystick[{args.joystick}] {joy.get_name()}")

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
        preview = payload if len(payload) <= 200 else payload[:200] + "..."
        print(f"[{ts()}] STATUS {preview}")

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

    interval = 1.0 / max(args.hz, 1.0)
    last_pub = 0.0
    last_snap: dict | None = None
    try:
        while True:
            snap = read_snapshot(joy)
            now = time.time()
            if now - last_pub >= interval and not snapshot_equal(snap, last_snap):
                payload = json.dumps(snap, separators=(",", ":"))
                client.publish(input_topic, payload, qos=0, retain=False)
                last_pub = now
                last_snap = snap
            time.sleep(0.01)
    except KeyboardInterrupt:
        print(f"[{ts()}] stopping; publish connected=false")
        off = {
            "connected": False,
            "source": "wifi",
            "axes": {"lx": 0, "ly": 0, "rx": 0, "ry": 0},
            "buttons": {
                "a": False,
                "b": False,
                "x": False,
                "y": False,
                "l1": False,
                "r1": False,
                "l2": 0,
                "r2": 0,
                "start": False,
                "select": False,
            },
            "ts": int(time.time()),
        }
        client.publish(input_topic, json.dumps(off), qos=0, retain=False)
        time.sleep(0.2)
    finally:
        client.loop_stop()
        client.disconnect()
        pygame.quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
