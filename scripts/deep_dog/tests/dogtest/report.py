"""结果收集 + JSON / Markdown 报告。"""

from __future__ import annotations

import json
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime


@dataclass
class Result:
    id: str
    feature: str
    domain: str
    title: str
    channel: str
    expect: str
    status: str            # PASS / FAIL / SKIP / BLOCKED / ERROR
    detail: str = ""
    duration_s: float = 0.0
    evidence: list = field(default_factory=list)


class Report:
    def __init__(self, cfg, argv: list[str] | None = None):
        self.cfg = cfg
        self.argv = argv or []
        self.started = time.time()
        self.results: list[Result] = []

    def add(self, r: Result) -> None:
        self.results.append(r)

    # ---- 统计 ----
    def counts(self) -> dict:
        c = {"PASS": 0, "FAIL": 0, "SKIP": 0, "BLOCKED": 0, "ERROR": 0}
        for r in self.results:
            c[r.status] = c.get(r.status, 0) + 1
        return c

    def by_domain(self) -> dict:
        out: dict[str, dict] = {}
        for r in self.results:
            d = out.setdefault(r.domain, {"PASS": 0, "FAIL": 0, "SKIP": 0, "BLOCKED": 0, "ERROR": 0})
            d[r.status] = d.get(r.status, 0) + 1
        return out

    # ---- 导出 ----
    def to_json(self) -> str:
        return json.dumps({
            "generated_at": datetime.now().isoformat(timespec="seconds"),
            "duration_s": round(time.time() - self.started, 1),
            "device": {
                "device_id": self.cfg.device_id, "ip": self.cfg.ip,
                "broker": f"{self.cfg.broker_host}:{self.cfg.broker_port}",
                "topic_root": self.cfg.topic_root,
            },
            "counts": self.counts(),
            "by_domain": self.by_domain(),
            "results": [asdict(r) for r in self.results],
        }, ensure_ascii=False, indent=2)

    def to_markdown(self) -> str:
        c = self.counts()
        total = sum(c.values())
        lines = [
            "# deep-dog 全盘功能测试报告（TEST_REPORT）",
            "",
            f"> 生成时间：{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
            f"> 设备：`{self.cfg.device_id}` · IP `{self.cfg.ip}` · "
            f"broker `{self.cfg.broker_host}:{self.cfg.broker_port}` · root `{self.cfg.topic_root}`",
            f"> 命令：`{' '.join(self.argv)}`",
            f"> 失败根因与契约冲突分析见 [catalog/FINDINGS.md](./catalog/FINDINGS.md)，"
            f"用例设计见 [catalog/TESTS.md](./catalog/TESTS.md)。",
            "",
            "## 1. 总览",
            "",
            "| 指标 | 值 |",
            "|------|-----|",
            f"| 用例总数 | {total} |",
            f"| PASS | {c['PASS']} |",
            f"| FAIL | {c['FAIL']} |",
            f"| SKIP | {c['SKIP']} |",
            f"| BLOCKED | {c['BLOCKED']} |",
            f"| ERROR | {c['ERROR']} |",
            f"| 通过率（PASS / 可执行） | "
            f"{self._rate(c)} |",
            "",
            "## 2. 分域结果",
            "",
            "| domain | PASS | FAIL | SKIP | BLOCKED | ERROR |",
            "|--------|------|------|------|---------|-------|",
        ]
        for dom, d in sorted(self.by_domain().items()):
            lines.append(f"| {dom} | {d['PASS']} | {d['FAIL']} | {d['SKIP']} | "
                         f"{d['BLOCKED']} | {d['ERROR']} |")

        fails = [r for r in self.results if r.status in ("FAIL", "ERROR")]
        lines += ["", "## 3. 未通过明细（FAIL / ERROR）", ""]
        if not fails:
            lines.append("无。")
        else:
            lines += ["| TC-ID | 功能点 | 用例 | 现象 |", "|-------|--------|------|------|"]
            for r in fails:
                lines.append(f"| {r.id} | {r.feature} | {r.title} | {self._esc(r.detail)} |")

        blocked = [r for r in self.results if r.status == "BLOCKED"]
        lines += ["", "## 4. 阻塞（BLOCKED）", ""]
        if not blocked:
            lines.append("无。")
        else:
            lines += ["| TC-ID | 功能点 | 用例 | 阻塞项 |", "|-------|--------|------|--------|"]
            for r in blocked:
                lines.append(f"| {r.id} | {r.feature} | {r.title} | {self._esc(r.detail)} |")

        skips = [r for r in self.results if r.status == "SKIP"]
        lines += ["", "## 5. 跳过（SKIP）", ""]
        if not skips:
            lines.append("无。")
        else:
            lines += ["| TC-ID | 功能点 | 用例 | 原因 |", "|-------|--------|------|------|"]
            for r in skips:
                lines.append(f"| {r.id} | {r.feature} | {r.title} | {self._esc(r.detail)} |")

        lines += ["", "## 6. 全量明细", "",
                  "| TC-ID | 功能点 | domain | 用例 | 通道 | 预期 | 状态 | 耗时(s) | 备注 |",
                  "|-------|--------|--------|------|------|------|------|---------|------|"]
        for r in sorted(self.results, key=lambda x: x.id):
            lines.append(f"| {r.id} | {r.feature} | {r.domain} | {r.title} | {r.channel} | "
                         f"{self._esc(r.expect)} | {r.status} | {r.duration_s:.2f} | "
                         f"{self._esc(r.detail)} |")
        lines.append("")
        return "\n".join(lines)

    @staticmethod
    def _rate(c: dict) -> str:
        executable = c["PASS"] + c["FAIL"] + c["ERROR"]
        if executable == 0:
            return "n/a"
        return f"{c['PASS'] / executable * 100:.1f}%"

    @staticmethod
    def _esc(text: str, limit: int = 160) -> str:
        t = (text or "").replace("\n", " ").replace("|", "\\|")
        return t if len(t) <= limit else t[:limit] + "…"
