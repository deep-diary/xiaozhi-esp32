"""执行引擎：构建上下文、运行用例、收集结果。"""

from __future__ import annotations

import importlib
import pkgutil
import time
import traceback
from typing import Optional

from . import clients, registry
from .registry import BlockedCase, Registry, SkipCase, Case
from .report import Report, Result


def _transport_exceptions() -> tuple:
    """「对端无响应/连接断开」类异常：判 BLOCKED 而非 ERROR（用例根本没跑起来）。

    覆盖 urllib（`TransportAnomaly`）、websockets（`ConnectionClosed`）、socket 超时/reset。
    """
    excs: list = [TimeoutError, ConnectionResetError, ConnectionRefusedError, OSError]
    try:
        from websockets.exceptions import ConnectionClosed, InvalidStatus
        excs += [ConnectionClosed, InvalidStatus]
    except Exception:
        pass
    excs.append(clients.TransportAnomaly)
    return tuple(excs)


TRANSPORT_EXC = _transport_exceptions()


class Ctx:
    """用例上下文：懒连接各客户端 + 便捷断言/日志。"""

    def __init__(self, cfg, log_fn=print):
        self.cfg = cfg
        self._log = log_fn
        self._mqtt: Optional[clients.MqttBus] = None
        self._http: Optional[clients.HttpClient] = None
        self._ws: Optional[clients.WsMcp] = None
        self._serial = None
        self._mqtt_error: Optional[str] = None
        self._ws_error: Optional[str] = None
        self.artifacts: list[str] = []

    # ---- 日志 ----
    def log(self, msg: str):
        self._log(f"    · {msg}")

    # ---- topic 助手 ----
    def topic(self, kind: str, subject: str) -> str:
        return f"{self.cfg.prefix()}/{kind}/{subject}"

    def cmd(self, subject: str) -> str:
        return self.topic("cmd", subject)

    def status(self, subject: str) -> str:
        return self.topic("status", subject)

    def info(self, subject: str) -> str:
        return self.topic("info", subject)

    # ---- 客户端 ----
    @property
    def mqtt(self) -> clients.MqttBus:
        if self._mqtt is None:
            if self._mqtt_error:
                raise ConnectionError(self._mqtt_error)
            try:
                bus = clients.MqttBus(self.cfg, client_id="dogtest")
                bus.connect(timeout=self.cfg.mqtt_timeout)
                self._mqtt = bus
            except Exception as exc:
                self._mqtt_error = str(exc)
                raise
        return self._mqtt

    @property
    def http(self) -> clients.HttpClient:
        if self._http is None:
            if not self.cfg.http_base:
                raise ConnectionError("http_base 未发现（设备 IP 未知）")
            self._http = clients.HttpClient(self.cfg.http_base, timeout=self.cfg.http_timeout)
        return self._http

    @property
    def wsmcp(self) -> clients.WsMcp:
        if self._ws is not None and self._ws.is_alive():
            return self._ws
        if self._ws is not None:
            # 上一用例期间被对端断开：丢弃旧 socket，重新握手（勿级联失败）
            try:
                self._ws.close()
            except Exception:
                pass
            self._ws = None
        if self._ws_error:
            raise ConnectionError(self._ws_error)
        if not self.cfg.ws_url:
            self._ws_error = "ws_url 未发现（设备 IP 未知）"
            raise ConnectionError(self._ws_error)
        try:
            self._ws = clients.WsMcp(self.cfg.ws_url).connect()
        except Exception as exc:
            self._ws_error = f"WS-MCP 连接失败 {self.cfg.ws_url}: {exc}"
            raise
        return self._ws

    @property
    def serial(self):
        """串口日志采集（首次访问会打开 COM6 → 触发 ESP32 复位，见 LL-006）。"""
        if self._serial is None:
            from .serialcap import SerialCapture
            self._serial = SerialCapture(self.cfg.serial_port).open()
        return self._serial

    # ---- 控制流 ----
    def skip(self, reason: str):
        raise SkipCase(reason)

    def blocked(self, reason: str):
        raise BlockedCase(reason)

    def require_mqtt(self):
        try:
            return self.mqtt
        except Exception as exc:
            self.blocked(f"MQTT 不可用：{exc}")

    # ---- 断言助手 ----
    @staticmethod
    def check(cond, msg: str):
        if not cond:
            raise AssertionError(msg)

    def close(self):
        if self._mqtt:
            self._mqtt.close()
        if self._ws:
            self._ws.close()
        if self._serial:
            self._serial.close()


def load_cases(package: str = "cases"):
    """导入 cases 包下全部模块，触发 @case 注册。"""
    try:
        pkg = importlib.import_module(package)
    except ImportError:
        return 0
    for mod in pkgutil.iter_modules(pkg.__path__):
        importlib.import_module(f"{package}.{mod.name}")
    return len(Registry)


def run(cases: list[Case], cfg, report: Report, verbose: bool = True,
        include_serial: bool = False) -> Report:
    log = print if verbose else (lambda *a, **k: None)
    # 串口用例会复位设备（LL-006）→ 默认排除；启用时**放到最后**执行，不影响网络用例。
    if include_serial:
        cases = [c for c in cases if "serial" not in c.tags] + \
                [c for c in cases if "serial" in c.tags]
    else:
        cases = [c for c in cases if "serial" not in c.tags]
    ctx = Ctx(cfg, log_fn=log)
    try:
        for c in cases:
            t0 = time.time()
            status, detail = "PASS", ""
            if not c.auto:
                status, detail = "SKIP", "非自动化用例（需人工 / 硬件物理确认）"
            else:
                try:
                    c.fn(ctx)
                except SkipCase as e:
                    status, detail = "SKIP", str(e)
                except BlockedCase as e:
                    status, detail = "BLOCKED", str(e)
                except AssertionError as e:
                    status, detail = "FAIL", str(e) or "断言失败"
                except ConnectionError as e:
                    status, detail = "BLOCKED", f"依赖不可用：{e}"
                except TRANSPORT_EXC as e:  # 对端无响应/断开：判 BLOCKED（用例无法执行，非业务失败）
                    status, detail = (
                        "BLOCKED",
                        f"传输级失败（对端无响应/断开，见 FINDINGS F-11/F-13）：{type(e).__name__}: {e}")
                except Exception as e:  # 用例内异常
                    status, detail = "ERROR", f"{type(e).__name__}: {e}"
                    if verbose:
                        traceback.print_exc()
            dur = round(time.time() - t0, 2)
            report.add(Result(
                id=c.id, feature=c.feature, domain=c.domain, title=c.title,
                channel=c.channel, expect=c.expect, status=status,
                detail=detail, duration_s=dur,
            ))
            mark = {"PASS": "PASS", "FAIL": "FAIL", "SKIP": "SKIP",
                    "BLOCKED": "BLOCK", "ERROR": "ERR "}.get(status, status)
            log(f"  [{mark}] {c.id} {c.title}" + (f"  → {detail}" if detail else ""))
    finally:
        ctx.close()
    return report
