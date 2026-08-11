#!/usr/bin/env python3
"""deep-dog 人脸功能 MQTT/WS 联调脚本（V-S07）。

用法:
  python3 scripts/iot/face_integration_test.py
  python3 scripts/iot/face_integration_test.py --device-id aabbcc --broker 192.168.31.25
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
import time
import uuid

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("pip install paho-mqtt", file=sys.stderr)
    sys.exit(2)

DEFAULT_DEVICE_ID = "1051db848fd4"
DEFAULT_BROKER = "192.168.31.25"
DEFAULT_PORT = 1883


def topic(prefix: str, suffix: str) -> str:
    return f"{prefix}/{suffix}"


def wait_live(client: mqtt.Client, prefix: str, seconds: float = 25.0) -> bool:
    """设备在线时 imu/status 约 20s 一条（non-retain）。"""
    seen = {"ok": False}

    def on_message(_c, _u, msg):
        if msg.topic.endswith("/imu/status") and not msg.retain:
            seen["ok"] = True

    old = client.on_message
    client.on_message = on_message
    client.subscribe(topic(prefix, "imu/status"), qos=0)
    deadline = time.time() + seconds
    while time.time() < deadline and not seen["ok"]:
        time.sleep(0.2)
    client.on_message = old
    return seen["ok"]


def main() -> int:
    parser = argparse.ArgumentParser(description="deep-dog face MQTT/WS integration test")
    parser.add_argument("--device-id", default=DEFAULT_DEVICE_ID)
    parser.add_argument("--broker", default=DEFAULT_BROKER)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--device-ip", default="", help="override IP from device/info")
    args = parser.parse_args()

    prefix = f"deepdiary/deep-dog/{args.device_id.strip().lower()}"
    received: dict[str, object] = {}
    checks: list[tuple[str, bool, str]] = []

    def check(name: str, ok: bool, detail: str = ""):
        checks.append((name, ok, detail))
        mark = "PASS" if ok else "FAIL"
        print(f"  [{mark}] {name}" + (f" — {detail}" if detail else ""))

    def on_connect(client, _userdata, _flags, reason_code, _props=None):
        rc = int(getattr(reason_code, "value", reason_code))
        if rc != 0:
            print(f"MQTT connect failed rc={reason_code}")
            return
        for suffix in ("device/info", "face/status", "face/registry", "person/active", "imu/status"):
            client.subscribe(topic(prefix, suffix), qos=0)
        print(f"subscribed {prefix}/…")

    def on_message(_client, _userdata, msg):
        suffix = msg.topic.rsplit("/", 1)[-1]
        # topic 末段可能重复（如 face/status 与 track/status 均为 status）
        if msg.topic.endswith("/face/status"):
            key_base = "face_status"
        elif msg.topic.endswith("/device/info"):
            key_base = "device_info"
        elif msg.topic.endswith("/face/registry"):
            key_base = "face_registry"
        elif msg.topic.endswith("/person/active"):
            key_base = "person_active"
        else:
            key_base = suffix.replace("/", "_")
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
        except Exception:
            payload = {"_raw": msg.payload.decode("utf-8", errors="replace")}
        key = f"{key_base}{'_retain' if msg.retain else '_live'}"
        received[key] = payload
        if not msg.retain and key_base in ("face_status", "face_registry", "device_info", "person_active"):
            print(f"[live] {msg.topic}\n{json.dumps(payload, ensure_ascii=False)[:800]}")

    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=f"face-it-{uuid.uuid4().hex[:8]}",
        protocol=mqtt.MQTTv311,
    )
    client.on_connect = on_connect
    client.on_message = on_message

    print(f"connecting {args.broker}:{args.port} …")
    client.connect(args.broker, args.port, keepalive=30)
    client.loop_start()
    time.sleep(2)

    print("\n=== Step 0: device online? (imu/status non-retain ≤25s) ===")
    online = wait_live(client, prefix, 25.0)
    check("device MQTT online", online, "无 imu/status 实时帧 → 设备未连 broker 或已离线")

    info = received.get("device_info_retain") or received.get("device_info_live")
    device_ip = args.device_ip or (info.get("ip") if isinstance(info, dict) else "") or ""

    if isinstance(info, dict):
        caps = info.get("capabilities") or {}
        check("capabilities.face", caps.get("face") is True)
        check("capabilities.ws_mcp", caps.get("ws_mcp") is True, str(caps.get("ws_mcp")))

    print("\n=== Step 1: retained face/registry ===")
    registry = received.get("face_registry_retain") or received.get("face_registry_live")
    if isinstance(registry, dict):
        check("face/registry.version", registry.get("version") == 1, str(registry.get("version")))
        check("face/registry.entries", isinstance(registry.get("entries"), list), f"count={registry.get('count')}")
    else:
        check("face/registry", False, "无 retain（需 V-S07 固件 + MQTT 已连接）")

    if not online:
        print("\n设备 MQTT 离线，跳过后续 cmd 测试。请 USB 烧录 feat/face 固件并确认 broker 在线。")
        client.loop_stop()
        client.disconnect()
        return summarize(checks)

    print("\n=== Step 2: enable detect + recognize ===")
    cmd = {
        "enabled": True,
        "recognize_enabled": True,
        "pipeline": "live",
        "detect_interval_ms": 500,
        "ts": int(time.time()),
    }
    client.publish(topic(prefix, "face/cmd"), json.dumps(cmd), qos=1)
    deadline = time.time() + 12.0
    status = None
    while time.time() < deadline:
        status = received.get("face_status_live")
        if isinstance(status, dict) and status.get("enabled") is True:
            break
        time.sleep(0.3)

    if isinstance(status, dict):
        check("face/status.enabled", status.get("enabled") is True, str(status.get("enabled")))
        recog = status.get("recognize_enabled")
        check(
            "face/status.recognize_enabled",
            recog is True,
            str(recog) if recog is not None else "字段缺失 → 旧固件",
        )
    else:
        check("face/status after cmd", False)

    print("\n=== Step 3: recognize off, detect on ===")
    client.publish(
        topic(prefix, "face/cmd"),
        json.dumps({"enabled": True, "recognize_enabled": False, "ts": int(time.time())}),
        qos=1,
    )
    received.pop("face_status_live", None)
    deadline = time.time() + 10.0
    status2 = None
    while time.time() < deadline:
        status2 = received.get("face_status_live")
        if isinstance(status2, dict) and status2.get("recognize_enabled") is False:
            break
        time.sleep(0.3)
    if isinstance(status2, dict):
        check(
            "detect on / recognize off",
            status2.get("enabled") is True and status2.get("recognize_enabled") is False,
            f"enabled={status2.get('enabled')} recognize={status2.get('recognize_enabled')}",
        )

    print("\n=== Step 4: restore recognize ===")
    client.publish(
        topic(prefix, "face/cmd"),
        json.dumps({"enabled": True, "recognize_enabled": True, "ts": int(time.time())}),
        qos=1,
    )
    time.sleep(2)

    if device_ip:
        print(f"\n=== Step 5: WS MCP ({device_ip}:8080/ws) ===")
        ws_ok = probe_ws_mcp(device_ip)
        check("WS MCP", ws_ok, f"ws://{device_ip}:8080/ws")
    else:
        check("WS MCP", False, "无 device/info.ip")

    client.loop_stop()
    client.disconnect()
    return summarize(checks)


def summarize(checks: list[tuple[str, bool, str]]) -> int:
    print("\n=== Summary ===")
    passed = sum(1 for _, ok, _ in checks if ok)
    for name, ok, detail in checks:
        print(f"  {'✓' if ok else '✗'} {name}: {detail}")
    print(f"\n{passed}/{len(checks)} checks passed")
    return 0 if passed == len(checks) else 1


def probe_ws_mcp(device_ip: str) -> bool:
    try:
        import websocket  # type: ignore
    except ImportError:
        try:
            with socket.create_connection((device_ip, 8080), timeout=2):
                print("  TCP 8080 open（安装 websocket-client 可测 tools/list）")
                return True
        except OSError as e:
            print(f"  TCP 8080: {e}")
            return False

    url = f"ws://{device_ip}:8080/ws"
    try:
        ws = websocket.create_connection(url, timeout=3)
        ws.send(
            json.dumps(
                {
                    "jsonrpc": "2.0",
                    "method": "initialize",
                    "params": {
                        "protocolVersion": "2024-11-05",
                        "capabilities": {},
                        "clientInfo": {"name": "face-it", "version": "1"},
                    },
                    "id": 1,
                }
            )
        )
        ws.settimeout(2)
        try:
            ws.recv()
        except Exception:
            pass

        all_names: list[str] = []
        cursor = ""
        for page in range(8):
            params: dict[str, str] = {}
            if cursor:
                params["cursor"] = cursor
            ws.send(json.dumps({"jsonrpc": "2.0", "method": "tools/list", "params": params, "id": 2 + page}))
            raw = ws.recv()
            data = json.loads(raw)
            result = data.get("result") or {}
            if isinstance(result, str):
                result = json.loads(result)
            tools = result.get("tools") or []
            all_names.extend(t.get("name") for t in tools if isinstance(t, dict))
            cursor = result.get("nextCursor") or ""
            if not cursor:
                break

        face_tools = [n for n in all_names if n and str(n).startswith("self.face.")]
        print(f"  face MCP tools: {face_tools}")
        ws.close()
        return len(face_tools) >= 3
    except Exception as e:
        print(f"  WS failed: {e}")
        return False


if __name__ == "__main__":
    raise SystemExit(main())
