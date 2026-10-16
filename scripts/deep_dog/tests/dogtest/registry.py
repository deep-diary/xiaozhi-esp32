"""用例注册表与 @case 装饰器。"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Optional

# case 函数签名：fn(ctx) -> None；失败 raise AssertionError；跳过 raise SkipCase
Registry: list["Case"] = []


class SkipCase(Exception):
    """用例主动跳过（附原因）。"""


class BlockedCase(Exception):
    """用例被外部条件阻塞（附原因）。"""


@dataclass
class Case:
    id: str                       # TC0001-device-info-publish
    feature: str                  # F0001
    domain: str                   # device
    title: str                    # 人读标题
    channel: str                  # MQTT / HTTP / WS-MCP / RTSP / 串口 / 内部
    expect: str                   # 预期
    fn: Callable
    auto: bool = True             # 是否可自动化（False → runner 默认 SKIP）
    tags: tuple = ()
    needs: tuple = ()             # 依赖：mqtt / http / wsmcp / rtsp / device
    slow: bool = False
    meta: dict = field(default_factory=dict)


def case(case_id: str, feature: str, domain: str, title: str, channel: str,
         expect: str, auto: bool = True, tags: tuple = (), needs: tuple = (),
         slow: bool = False):
    """把一个函数注册为测试用例。"""
    def deco(fn: Callable) -> Callable:
        Registry.append(Case(
            id=case_id, feature=feature, domain=domain, title=title,
            channel=channel, expect=expect, fn=fn, auto=auto,
            tags=tuple(tags), needs=tuple(needs), slow=slow,
        ))
        return fn
    return deco


def select(domain: Optional[str] = None, case_id: Optional[str] = None,
           only_auto: bool = False, tag: Optional[str] = None) -> list[Case]:
    out = []
    for c in Registry:
        if domain and c.domain != domain:
            continue
        if case_id and case_id.lower() not in c.id.lower():
            continue
        if only_auto and not c.auto:
            continue
        if tag and tag not in c.tags:
            continue
        out.append(c)
    out.sort(key=lambda c: c.id)
    return out
