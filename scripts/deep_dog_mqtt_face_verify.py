#!/usr/bin/env python3
"""Verify deep-dog MQTT face/cmd + face/status (LAN and/or WSS).

Suggested order: stop RTSP push first so VisionHub feeds face_ai
  /usr/bin/python3 scripts/deep_dog_mqtt_verify.py --via web --stop-stream --wait 5

Then:
  /usr/bin/python3 scripts/deep_dog_mqtt_face_verify.py --via web --wait 12
  /usr/bin/python3 scripts/deep_dog_mqtt_face_verify.py --via both --wait 10
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

WEB_WSS_DEFAULT = "wss://mqtt-ws.deep-diary.com/mqtt"
LAN_HOST_DEFAULT = "192.168.31.25"
LAN_PORT_DEFAULT = 1883

STATUS_REQUIRED = ("enabled", "has_person", "n", "w", "h", "faces", "ts")


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


def validate_status(obj: dict) -> list[str]:
    errs: list[str] = []
    for k in STATUS_REQUIRED:
        if k not in obj:
            errs.append(f"missing:{k}")
    if "enabled" in obj and not isinstance(obj["enabled"], bool):
        errs.append("enabled_not_bool")
    if "has_person" in obj and not isinstance(obj["has_person"], bool):
        errs.append("has_person_not_bool")
    if "faces" in obj and not isinstance(obj["faces"], list):
        errs.append("faces_not_array")
    if obj.get("has_person") is True:
        if "primary" not in obj or not isinstance(obj["primary"], dict):
            errs.append("missing_primary")
        else:
            for k in ("cx", "cy"):
                if k not in obj["primary"]:
                    errs.append(f"primary_missing:{k}")
        if isinstance(obj.get("faces"), list) and len(obj["faces"]) < 1:
            errs.append("has_person_empty_faces")
        # pixel coords should be roughly within frame (allow slight overflow)
        w, h = obj.get("w"), obj.get("h")
        if isinstance(w, (int, float)) and isinstance(h, (int, float)) and w > 0 and h > 0:
            for i, f in enumerate(obj.get("faces") or []):
                for k in ("x0", "y0", "x1", "y1", "cx", "cy"):
                    v = f.get(k)
                    if not isinstance(v, (int, float)):
                        errs.append(f"face{i}_{k}_bad")
                    elif k in ("x0", "x1", "cx") and not (-10 <= float(v) <= float(w) + 10):
                        errs.append(f"face{i}_{k}_out")
                    elif k in ("y0", "y1", "cy") and not (-10 <= float(v) <= float(h) + 10):
                        errs.append(f"face{i}_{k}_out")
    return errs


def run_once(via: str, args: argparse.Namespace) -> int:
    prefix = f"deepdiary/deep-dog/{args.device_id}"
    status_topic = f"{prefix}/face/status"
    cmd_topic = f"{prefix}/face/cmd"
    info_topic = f"{prefix}/device/info"
    topics = [(status_topic, 0), (info_topic, 0)]

    seen: dict[str, int] = {t: 0 for t, _ in topics}
    statuses: list[dict] = []
    field_errors: list[str] = []
    connected = {"ok": False, "rc": None}
    caps_face = {"seen": False, "value": None}
    enabled_seen: list[bool] = []

    use_ws = via == "web"
    transport = "websockets" if use_ws else "tcp"
    client_id = f"deep-dog-face-verify-{via}-{int(time.time())}"

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
        print(f"         {payload if len(payload) <= 320 else payload[:320] + '...'}")
        try:
            obj = json.loads(payload)
        except json.JSONDecodeError as e:
            field_errors.append(f"json:{e}")
            return
        if msg.topic.endswith("device/info"):
            caps = obj.get("capabilities") or {}
            if "face" in caps:
                caps_face["seen"] = True
                caps_face["value"] = bool(caps["face"])
            return
        if msg.topic.endswith("face/status"):
            field_errors.extend(validate_status(obj))
            if isinstance(obj.get("enabled"), bool):
                enabled_seen.append(obj["enabled"])
            if len(statuses) < 5:
                statuses.append(obj)

    client = make_client(client_id, transport)
    if args.username:
        client.username_pw_set(args.username, args.password)
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

    def pub_enabled(on: bool) -> None:
        body = {"enabled": on, "ts": int(time.time())}
        payload = json.dumps(body, separators=(",", ":"))
        info = client.publish(cmd_topic, payload, qos=1)
        info.wait_for_publish(timeout=5)
        print(f"[{ts()}] PUB {cmd_topic} {payload}")

    # collect a bit, then toggle off/on
    time.sleep(min(2.0, args.wait * 0.2))
    if args.toggle:
        pub_enabled(False)
        time.sleep(1.5)
        pub_enabled(True)

    end = time.time() + args.wait
    while time.time() < end:
        time.sleep(0.1)

    client.loop_stop()
    client.disconnect()

    print(f"\n=== summary via={via} ===")
    print(f"  face/status count={seen.get(status_topic, 0)} (min={args.min_msgs})")
    print(f"  device/info count={seen.get(info_topic, 0)}")
    if caps_face["seen"]:
        print(f"  capabilities.face={caps_face['value']}")
    if statuses:
        print("  sample face/status:")
        print("  " + json.dumps(statuses[0], ensure_ascii=False))

    ok = True
    if seen.get(status_topic, 0) < args.min_msgs:
        print(f"FAIL: need >= {args.min_msgs} face/status", file=sys.stderr)
        ok = False
    uniq_errs = sorted(set(field_errors))
    if uniq_errs:
        print(f"FAIL: field errors: {uniq_errs}", file=sys.stderr)
        ok = False
    if caps_face["seen"] and caps_face["value"] is not True:
        print("WARN: capabilities.face is not true", file=sys.stderr)
    if args.toggle:
        if False not in enabled_seen:
            print("FAIL: never observed enabled=false after cmd", file=sys.stderr)
            ok = False
        if True not in enabled_seen:
            print("FAIL: never observed enabled=true after cmd", file=sys.stderr)
            ok = False
        else:
            print("  toggle: saw enabled false/true OK")
    if ok:
        print("PASS: face mqtt OK")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify deep-dog MQTT face module")
    ap.add_argument("--via", choices=["web", "lan", "both"], default="web")
    ap.add_argument("--wss", default=WEB_WSS_DEFAULT)
    ap.add_argument("--broker", default=LAN_HOST_DEFAULT)
    ap.add_argument("--port", type=int, default=LAN_PORT_DEFAULT)
    ap.add_argument("--device-id", default="dev")
    ap.add_argument("--username", default=os.environ.get("DEEP_DOG_MQTT_USER", ""))
    ap.add_argument("--password", default=os.environ.get("DEEP_DOG_MQTT_PASS", ""))
    ap.add_argument("--wait", type=float, default=12.0)
    ap.add_argument("--min-msgs", type=int, default=2)
    ap.add_argument("--toggle", action="store_true", default=True, help="send face/cmd off then on (default)")
    ap.add_argument("--no-toggle", action="store_false", dest="toggle")
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
