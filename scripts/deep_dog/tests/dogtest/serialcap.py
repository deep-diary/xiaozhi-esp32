"""串口日志采集 + 断言（用于只能从串口观测的功能点）。

注意（LL-006）：**用 pyserial 打开 COM6 会触发 ESP32 自动复位**。
因此串口用例天然是「从冷启动开始观测」，且必须在网络用例**之后**单独跑
（见 runner 的 include_serial 排序），否则会把设备重启掉影响其它用例。
"""

from __future__ import annotations

import re
import threading
import time


class SerialCapture:
    def __init__(self, port: str, baud: int = 115200, ring: int = 6000):
        self.port = port
        self.baud = baud
        self._ring = ring
        self._lines: list[tuple[float, str]] = []
        self._lock = threading.Lock()
        self._stop = False
        self._thread: threading.Thread | None = None
        self._ser = None

    # ---- 生命周期 ----
    def open(self) -> "SerialCapture":
        import serial  # pyserial
        self._ser = serial.Serial(self.port, self.baud, timeout=0.3)
        # 拉低 DTR/RTS：打开端口本身已产生复位脉冲，这里保持非下载态
        self._ser.setDTR(False)
        self._ser.setRTS(False)
        self._thread = threading.Thread(target=self._loop, name="serial-capture", daemon=True)
        self._thread.start()
        return self

    def close(self):
        self._stop = True
        if self._thread:
            self._thread.join(timeout=1.0)
        try:
            if self._ser:
                self._ser.close()
        except Exception:
            pass

    def _loop(self):
        buf = ""
        while not self._stop:
            try:
                chunk = self._ser.read(4096)
            except Exception:
                break
            if not chunk:
                continue
            buf += chunk.decode("utf-8", errors="replace")
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                with self._lock:
                    self._lines.append((time.time(), line.rstrip("\r")))
                    if len(self._lines) > self._ring:
                        del self._lines[: len(self._lines) - self._ring]

    # ---- 读取 ----
    def mark(self) -> float:
        return time.time()

    def wait(self, pattern: str, timeout: float = 30.0, since: float = 0.0) -> str | None:
        """等待匹配 pattern 的日志行，返回该行；超时返回 None。"""
        rx = re.compile(pattern)
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self._lock:
                for ts, line in self._lines:
                    if ts >= since and rx.search(line):
                        return line
            time.sleep(0.05)
        return None

    def search(self, pattern: str, since: float = 0.0) -> str | None:
        rx = re.compile(pattern)
        with self._lock:
            for ts, line in self._lines:
                if ts >= since and rx.search(line):
                    return line
        return None

    def tail(self, n: int = 25) -> str:
        with self._lock:
            return "\n".join(l for _, l in self._lines[-n:])
