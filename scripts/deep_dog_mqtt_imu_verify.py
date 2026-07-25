#!/usr/bin/env python3
"""Verify deep-dog MQTT imu/status on LAN and/or WSS (for frontend contract).

Examples:
  /usr/bin/python3 scripts/deep_dog_mqtt_imu_verify.py --via web --wait 8
  /usr/bin/python3 scripts/deep_dog_mqtt_imu_verify.py --via lan --wait 8
  /usr/bin/python3 scripts/deep_dog_mqtt_imu_verify.py --via both --min-msgs 5
"""

from __future__ import annotations

import argparse
import json
import math
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

WEB_WSS_DEFAULT = "wss://mqtt-ws.deep-diary.com/mqtt"
LAN_HOST_DEFAULT = "192.168.31.25"
LAN_PORT_DEFAULT = 1883

REQUIRED_FIELDS = (
    "ok",
    "accel_x",
    "accel_y",
    "accel_z",
    "accel_g",
    "gyro_x",
    "gyro_y",
    "gyro_z",
    "pitch",
    "roll",
    "ts",
)

SWITCH_KEYS = (
    "rot_x_pos",
    "rot_x_neg",
    "rot_y_pos",
    "rot_y_neg",
    "rot_z_pos",
    "rot_z_neg",
    "trans_x_pos",
    "trans_x_neg",
    "trans_y_pos",
    "trans_y_neg",
    "trans_z_pos",
    "trans_z_neg",
)


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


def validate_payload(obj: dict) -> list[str]:
    errs: list[str] = []
    for k in REQUIRED_FIELDS:
        if k not in obj:
            errs.append(f"missing:{k}")
    if "ok" in obj and not isinstance(obj["ok"], bool):
        errs.append("ok_not_bool")
    for k in REQUIRED_FIELDS:
        if k == "ok" or k not in obj:
            continue
        if not isinstance(obj[k], (int, float)):
            errs.append(f"not_number:{k}")
    switches = obj.get("switches")
    if not isinstance(switches, dict):
        errs.append("missing:switches")
    else:
        for sk in SWITCH_KEYS:
            if sk not in switches:
                errs.append(f"missing:switches.{sk}")
                continue
            v = switches[sk]
            if not isinstance(v, (int, float)) or int(v) != v or int(v) < 0:
                errs.append(f"bad_switch:{sk}")
    if obj.get("ok") is True:
        ag = obj.get("accel_g")
        if isinstance(ag, (int, float)) and not (0.5 <= float(ag) <= 30.0):
            errs.append(f"accel_g_out_of_range:{ag}")
        # optional consistency check
        try:
            ax, ay, az = float(obj["accel_x"]), float(obj["accel_y"]), float(obj["accel_z"])
            calc = math.sqrt(ax * ax + ay * ay + az * az)
            if abs(calc - float(obj["accel_g"])) > 0.5:
                errs.append("accel_g_mismatch")
        except Exception:
            pass
    return errs


def run_once(via: str, args: argparse.Namespace) -> int:
    prefix = f"deepdiary/deep-dog/{args.device_id}"
    imu_topic = f"{prefix}/imu/status"
    info_topic = f"{prefix}/device/info"
    topics = [(imu_topic, 0), (info_topic, 0)]

    seen: dict[str, int] = {t: 0 for t, _ in topics}
    samples: list[dict] = []
    field_errors: list[str] = []
    connected = {"ok": False, "rc": None}
    caps_imu = {"seen": False, "value": None}

    use_ws = via == "web"
    transport = "websockets" if use_ws else "tcp"
    client_id = f"deep-dog-imu-verify-{via}-{int(time.time())}"

    def on_connect(client, userdata, flags, reason_code, properties=None):
        rc = reason_code if isinstance(reason_code, int) else getattr(reason_code, "value", reason_code)
        connected["rc"] = rc
        if rc != 0:
            print(f"[{ts()}] CONNECT failed via={via} rc={rc}", file=sys.stderr)
            return
        connected["ok"] = True
        endpoint = args.wss if use_ws else f"{args.broker}:{args.port}"
        print(f"[{ts()}] CONNECTED via={via} {endpoint}")
        for topic, qos in topics:
            client.subscribe(topic, qos=qos)
            print(f"[{ts()}] SUB {topic} qos={qos}")

    def on_message(client, userdata, msg):
        payload = msg.payload.decode("utf-8", errors="replace")
        seen[msg.topic] = seen.get(msg.topic, 0) + 1
        retain = " retain" if getattr(msg, "retain", False) else ""
        print(f"[{ts()}] MSG{retain} {msg.topic}")
        print(f"         {payload if len(payload) <= 280 else payload[:280] + '...'}")
        try:
            obj = json.loads(payload)
        except json.JSONDecodeError as e:
            field_errors.append(f"json:{e}")
            return
        if msg.topic.endswith("device/info"):
            caps = (obj.get("capabilities") or {})
            if "imu" in caps:
                caps_imu["seen"] = True
                caps_imu["value"] = bool(caps["imu"])
            return
        if msg.topic.endswith("imu/status"):
            errs = validate_payload(obj)
            field_errors.extend(errs)
            if len(samples) < 3:
                samples.append(obj)

    client = make_client(client_id, transport)
    if args.username:
        client.username_pw_set(args.username, args.password)
        print(f"[{ts()}] auth user={args.username}")
    else:
        print(f"[{ts()}] auth anonymous")

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
            print(f"[{ts()}] connecting WSS {host}:{port}{path}")
            client.connect(host, port, keepalive=30)
        else:
            import socket

            infos = socket.getaddrinfo(args.broker, args.port, socket.AF_INET, socket.SOCK_STREAM)
            host_ip = infos[0][4][0] if infos else args.broker
            print(f"[{ts()}] connecting LAN {args.broker}:{args.port} ({host_ip})")
            client.connect(host_ip, args.port, keepalive=30)
    except OSError as e:
        print(f"connect failed via={via}: {e}", file=sys.stderr)
        return 1

    client.loop_start()
    deadline = time.time() + 12.0
    while time.time() < deadline and not connected["ok"]:
        time.sleep(0.1)
    if not connected["ok"]:
        print(f"broker connect fail via={via} rc={connected['rc']}", file=sys.stderr)
        client.loop_stop()
        return 1

    end = time.time() + args.wait
    while time.time() < end:
        time.sleep(0.1)

    client.loop_stop()
    client.disconnect()

    print(f"\n=== summary via={via} ===")
    print(f"  imu/status count={seen.get(imu_topic, 0)} (min={args.min_msgs})")
    print(f"  device/info count={seen.get(info_topic, 0)}")
    if caps_imu["seen"]:
        print(f"  capabilities.imu={caps_imu['value']}")
    if samples:
        print("  sample imu/status:")
        print("  " + json.dumps(samples[0], ensure_ascii=False))

    ok = True
    if seen.get(imu_topic, 0) < args.min_msgs:
        print(f"FAIL: need >= {args.min_msgs} imu/status messages", file=sys.stderr)
        ok = False
    if field_errors:
        uniq = sorted(set(field_errors))
        print(f"FAIL: field errors: {uniq}", file=sys.stderr)
        ok = False
    if caps_imu["seen"] and caps_imu["value"] is not True:
        print("WARN: capabilities.imu is not true (frontend may hide IMU card)", file=sys.stderr)
    if ok:
        print("PASS: imu/status received and fields OK")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify deep-dog MQTT imu/status")
    ap.add_argument("--via", choices=["web", "lan", "both"], default="web")
    ap.add_argument("--wss", default=WEB_WSS_DEFAULT)
    ap.add_argument("--broker", default=LAN_HOST_DEFAULT)
    ap.add_argument("--port", type=int, default=LAN_PORT_DEFAULT)
    ap.add_argument("--device-id", default="dev")
    ap.add_argument("--username", default=os.environ.get("DEEP_DOG_MQTT_USER", ""))
    ap.add_argument("--password", default=os.environ.get("DEEP_DOG_MQTT_PASS", ""))
    ap.add_argument("--wait", type=float, default=8.0)
    ap.add_argument("--min-msgs", type=int, default=3, help="minimum imu/status messages")
    args = ap.parse_args()

    vias = ["lan", "web"] if args.via == "both" else [args.via]
    rc = 0
    for v in vias:
        r = run_once(v, args)
        if r != 0:
            rc = r
    return rc


if __name__ == "__main__":
    sys.exit(main())
