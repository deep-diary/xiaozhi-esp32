#!/usr/bin/env python3
"""Verify deep-dog MQTT topics the same way the web frontend does.

Default path: WSS 外网（与网页一致）
  wss://mqtt-ws.deep-diary.com/mqtt

LAN path（设备侧直连，可选）:
  mqtt://192.168.31.25:1883

Credentials（勿写入仓库）:
  export DEEP_DOG_MQTT_USER=...
  export DEEP_DOG_MQTT_PASS=...
  或 --username / --password

Examples:
  /usr/bin/python3 scripts/deep_dog_mqtt_verify.py --wait 30 --start-stream
  /usr/bin/python3 scripts/deep_dog_mqtt_verify.py --via lan --wait 20
  /usr/bin/python3 scripts/deep_dog_mqtt_verify.py --via web --username USER --password PASS
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


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify deep-dog MQTT topics (web WSS by default)")
    ap.add_argument(
        "--via",
        choices=["web", "lan"],
        default="web",
        help="web=外网 WSS（默认，对齐前端）；lan=局域网 TCP",
    )
    ap.add_argument("--wss", default=WEB_WSS_DEFAULT, help="WSS URL when --via web")
    ap.add_argument("--broker", default=LAN_HOST_DEFAULT, help="host when --via lan")
    ap.add_argument("--port", type=int, default=LAN_PORT_DEFAULT, help="port when --via lan")
    ap.add_argument("--device-id", default="dev")
    ap.add_argument("--username", default=os.environ.get("DEEP_DOG_MQTT_USER", ""))
    ap.add_argument("--password", default=os.environ.get("DEEP_DOG_MQTT_PASS", ""))
    ap.add_argument("--wait", type=float, default=30.0, help="seconds to listen after connect")
    ap.add_argument("--start-stream", action="store_true", help="publish stream/cmd start")
    ap.add_argument("--stop-stream", action="store_true", help="publish stream/cmd stop")
    ap.add_argument("--cmd-delay", type=float, default=3.0, help="delay before sending cmd")
    args = ap.parse_args()

    prefix = f"deepdiary/deep-dog/{args.device_id}"
    topics = [
        (f"{prefix}/device/info", 0),
        (f"{prefix}/device/status", 0),
        (f"{prefix}/stream/status", 0),
    ]
    cmd_topic = f"{prefix}/stream/cmd"

    seen: dict[str, int] = {t: 0 for t, _ in topics}
    connected = {"ok": False, "rc": None}

    use_ws = args.via == "web"
    transport = "websockets" if use_ws else "tcp"
    client_id = f"deep-dog-mqtt-verify-{int(time.time())}"

    def on_connect(client, userdata, flags, reason_code, properties=None):
        rc = reason_code if isinstance(reason_code, int) else getattr(reason_code, "value", reason_code)
        connected["rc"] = rc
        if rc != 0:
            print(f"[{ts()}] CONNECT failed rc={rc}", file=sys.stderr)
            return
        connected["ok"] = True
        endpoint = args.wss if use_ws else f"{args.broker}:{args.port}"
        print(f"[{ts()}] CONNECTED via={args.via} {endpoint}")
        for topic, qos in topics:
            client.subscribe(topic, qos=qos)
            print(f"[{ts()}] SUB {topic} qos={qos}")

    def on_message(client, userdata, msg):
        payload = msg.payload.decode("utf-8", errors="replace")
        seen[msg.topic] = seen.get(msg.topic, 0) + 1
        retain = " retain" if getattr(msg, "retain", False) else ""
        preview = payload if len(payload) <= 240 else payload[:240] + "..."
        print(f"[{ts()}] MSG{retain} {msg.topic}")
        print(f"         {preview}")

    client = make_client(client_id, transport)
    if args.username:
        client.username_pw_set(args.username, args.password)
        print(f"[{ts()}] auth user={args.username}")
    else:
        print(f"[{ts()}] auth anonymous (set DEEP_DOG_MQTT_USER/PASS if broker requires login)")

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
            print(f"[{ts()}] connecting WSS {host}:{port}{path} prefix={prefix}/")
            client.connect(host, port, keepalive=30)
        else:
            import socket

            infos = socket.getaddrinfo(args.broker, args.port, socket.AF_INET, socket.SOCK_STREAM)
            host_ip = infos[0][4][0] if infos else args.broker
            print(f"[{ts()}] connecting LAN {args.broker}:{args.port} ({host_ip}) prefix={prefix}/")
            client.connect(host_ip, args.port, keepalive=30)
    except OSError as e:
        print(f"connect failed: {e}", file=sys.stderr)
        return 1

    client.loop_start()
    deadline = time.time() + 12.0
    while time.time() < deadline and not connected["ok"] and connected["rc"] is None:
        time.sleep(0.1)
    # wait a bit more for CONNACK
    while time.time() < deadline and not connected["ok"] and connected["rc"] is None:
        time.sleep(0.1)
    time.sleep(0.5)
    if not connected["ok"]:
        print(
            f"broker connect timeout/fail rc={connected['rc']} "
            f"(try --username/--password or DEEP_DOG_MQTT_USER/PASS)",
            file=sys.stderr,
        )
        client.loop_stop()
        return 1

    def pub_cmd(action: str) -> None:
        body = {"action": action, "ts": int(time.time())}
        if action == "start":
            body["mode"] = "rtsp_push"
        payload = json.dumps(body, separators=(",", ":"))
        info = client.publish(cmd_topic, payload, qos=1)
        info.wait_for_publish(timeout=5)
        print(f"[{ts()}] PUB {cmd_topic} {payload}")

    end = time.time() + args.wait
    cmd_at = time.time() + args.cmd_delay
    cmd_done = False
    while time.time() < end:
        if not cmd_done and time.time() >= cmd_at:
            if args.start_stream:
                pub_cmd("start")
            elif args.stop_stream:
                pub_cmd("stop")
            cmd_done = True
        time.sleep(0.2)

    client.loop_stop()
    client.disconnect()

    print("\n=== summary ===")
    print(f"via={args.via}")
    ok = True
    for topic, _ in topics:
        n = seen.get(topic, 0)
        mark = "OK" if n > 0 else "MISS"
        if n == 0 and (topic.endswith("device/info") or topic.endswith("device/status")):
            ok = False
        print(f"  [{mark}] {topic} count={n}")
    if not ok:
        print("FAIL: required device topics not received over this path", file=sys.stderr)
        return 1
    print("PASS: device topics received")
    return 0


if __name__ == "__main__":
    sys.exit(main())
