#!/usr/bin/env python3
"""RTSP 推流 + 人脸开关压测（V-S08 / 02-stream）。

循环 start/stop 推流并切换 face 检测/识别，断言 voice_paused 与无 reboot。

用法:
  python3 scripts/deep_dog/stream_face_stress_test.py
  python3 scripts/deep_dog/stream_face_stress_test.py --loops 5 --broker 192.168.31.25
  python3 scripts/deep_dog/stream_face_stress_test.py --auto-stop-wait 310
"""

from __future__ import annotations

import argparse
import json
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
DEFAULT_LOOPS = 5


def topic(prefix: str, suffix: str) -> str:
    return f"{prefix}/{suffix}"


def wait_status(
    received: dict,
    *,
    mode: str | None = None,
    state: str | None = None,
    voice_paused: bool | None = None,
    error: str | None = None,
    timeout: float = 30.0,
) -> dict | None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        st = received.get("stream_status_live") or received.get("stream_status_retain")
        if isinstance(st, dict):
            ok = True
            if mode is not None and st.get("mode") != mode:
                ok = False
            if state is not None and st.get("state") != state:
                ok = False
            if voice_paused is not None and st.get("voice_paused") is not voice_paused:
                ok = False
            if error is not None and st.get("error") != error:
                ok = False
            if ok:
                return st
        time.sleep(0.25)
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="deep-dog RTSP + face stress test")
    parser.add_argument("--device-id", default=DEFAULT_DEVICE_ID)
    parser.add_argument("--broker", default=DEFAULT_BROKER)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--loops", type=int, default=DEFAULT_LOOPS)
    parser.add_argument(
        "--auto-stop-wait",
        type=int,
        default=0,
        help="若 >0：单独测 5min 自动停流，等待秒数（建议 310）",
    )
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
        for suffix in ("device/info", "stream/status", "face/status", "imu/status"):
            client.subscribe(topic(prefix, suffix), qos=0)

    def on_message(_client, _userdata, msg):
        key_base = msg.topic.rsplit("/", 1)[-1]
        if msg.topic.endswith("/stream/status"):
            key_base = "stream_status"
        elif msg.topic.endswith("/face/status"):
            key_base = "face_status"
        elif msg.topic.endswith("/device/info"):
            key_base = "device_info"
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
        except Exception:
            payload = {"_raw": msg.payload.decode("utf-8", errors="replace")}
        key = f"{key_base}{'_retain' if msg.retain else '_live'}"
        received[key] = payload

    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=f"stream-stress-{uuid.uuid4().hex[:8]}",
        protocol=mqtt.MQTTv311,
    )
    client.on_connect = on_connect
    client.on_message = on_message

    print(f"connecting {args.broker}:{args.port} …")
    client.connect(args.broker, args.port, keepalive=30)
    client.loop_start()
    time.sleep(2)

    info0 = received.get("device_info_retain") or received.get("device_info_live")
    uptime0 = info0.get("uptime_s") if isinstance(info0, dict) else None
    reset0 = info0.get("reset_reason") if isinstance(info0, dict) else None
    check("device/info baseline", isinstance(info0, dict), f"uptime_s={uptime0} reset={reset0}")

    stream_cmd = topic(prefix, "stream/cmd")
    face_cmd = topic(prefix, "face/cmd")
    now_ts = lambda: int(time.time())

    if args.auto_stop_wait > 0:
        print(f"\n=== Auto-stop test (wait {args.auto_stop_wait}s) ===")
        received.pop("stream_status_live", None)
        client.publish(stream_cmd, json.dumps({"action": "start", "ts": now_ts()}), qos=1)
        st = wait_status(received, mode="rtsp_push", voice_paused=True, timeout=25.0)
        check("auto-stop: streaming + voice_paused", st is not None, str(st))
        deadline = time.time() + args.auto_stop_wait
        st2 = None
        while time.time() < deadline:
            st2 = received.get("stream_status_live") or received.get("stream_status_retain")
            if (
                isinstance(st2, dict)
                and st2.get("mode") == "off"
                and st2.get("error") == "auto_stop_timeout"
            ):
                break
            time.sleep(1.0)
        check(
            "auto-stop: idle + auto_stop_timeout",
            isinstance(st2, dict)
            and st2.get("mode") == "off"
            and st2.get("error") == "auto_stop_timeout",
            str(st2),
        )
        st3 = wait_status(received, mode="off", voice_paused=False, timeout=15.0)
        check("auto-stop: voice restored", st3 is not None, str(st3))
    else:
        print(f"\n=== Stress loops N={args.loops} ===")
        for i in range(args.loops):
            print(f"\n--- loop {i + 1}/{args.loops} ---")
            received.pop("stream_status_live", None)
            client.publish(stream_cmd, json.dumps({"action": "start", "ts": now_ts()}), qos=1)
            st = wait_status(received, mode="rtsp_push", voice_paused=True, timeout=25.0)
            check(f"loop{i+1}: start rtsp + voice_paused", st is not None, str(st))

            client.publish(
                face_cmd,
                json.dumps({"enabled": False, "ts": now_ts()}),
                qos=1,
            )
            time.sleep(1.5)
            client.publish(
                face_cmd,
                json.dumps(
                    {
                        "enabled": True,
                        "recognize_enabled": True,
                        "detect_interval_ms": 500,
                        "ts": now_ts(),
                    }
                ),
                qos=1,
            )
            time.sleep(1.5)
            client.publish(
                face_cmd,
                json.dumps({"enabled": True, "recognize_enabled": False, "ts": now_ts()}),
                qos=1,
            )
            time.sleep(1.0)
            client.publish(
                face_cmd,
                json.dumps({"enabled": True, "recognize_enabled": True, "ts": now_ts()}),
                qos=1,
            )
            time.sleep(2.0)

            received.pop("stream_status_live", None)
            client.publish(stream_cmd, json.dumps({"action": "stop", "ts": now_ts()}), qos=1)
            st_off = wait_status(received, mode="off", voice_paused=False, timeout=25.0)
            check(f"loop{i+1}: stop + voice restored", st_off is not None, str(st_off))
            time.sleep(1.0)

        print("\n=== http_stream_disabled (mode=stream) ===")
        received.pop("stream_status_live", None)
        client.publish(
            stream_cmd,
            json.dumps({"action": "start", "mode": "stream", "ts": now_ts()}),
            qos=1,
        )
        st_err = wait_status(received, error="http_stream_disabled", timeout=10.0)
        check("http stream rejected", st_err is not None, str(st_err))

    time.sleep(2.0)
    info1 = received.get("device_info_retain") or received.get("device_info_live")
    uptime1 = info1.get("uptime_s") if isinstance(info1, dict) else None
    reset1 = info1.get("reset_reason") if isinstance(info1, dict) else None
    rebooted = False
    if isinstance(uptime0, (int, float)) and isinstance(uptime1, (int, float)):
        if uptime1 < uptime0 - 5:
            rebooted = True
    if reset1 in ("watchdog", "panic"):
        rebooted = True
    check("no reboot during test", not rebooted, f"uptime {uptime0} -> {uptime1} reset={reset1}")

    client.loop_stop()
    client.disconnect()

    print("\n=== Summary ===")
    passed = sum(1 for _, ok, _ in checks if ok)
    for name, ok, detail in checks:
        print(f"  {'✓' if ok else '✗'} {name}: {detail}")
    print(f"\n{passed}/{len(checks)} checks passed")
    return 0 if passed == len(checks) else 1


if __name__ == "__main__":
    sys.exit(main())
