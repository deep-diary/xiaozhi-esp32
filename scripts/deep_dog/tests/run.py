#!/usr/bin/env python3
"""deep-dog 自动化测试入口。

示例：
  python scripts/deep_dog/tests/run.py --discover auto                 # 全量（自动发现设备）
  python scripts/deep_dog/tests/run.py --domain motor                  # 只跑某域
  python scripts/deep_dog/tests/run.py --case TC1071                   # 只跑某用例（子串匹配）
  python scripts/deep_dog/tests/run.py --broker 192.168.3.73 --device-id aabbccddeeff
  python scripts/deep_dog/tests/run.py --no-report                     # 不落盘报告

报告输出：main/boards/deep-dog/docs/requirements/TEST_REPORT.md（+ .json 同名）
"""

from __future__ import annotations

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from dogtest import config as cfgmod          # noqa: E402
from dogtest import registry as reg           # noqa: E402
from dogtest import runner as runmod          # noqa: E402
from dogtest.report import Report             # noqa: E402

REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
REPORT_DIR = os.path.join(REPO_ROOT, "main", "boards", "deep-dog", "docs", "requirements")
REPORT_MD = os.path.join(REPORT_DIR, "TEST_REPORT.md")
REPORT_JSON = os.path.join(REPORT_DIR, "TEST_REPORT.json")


def main() -> int:
    ap = argparse.ArgumentParser(description="deep-dog 全盘功能自动化测试")
    cfgmod.add_args(ap)
    ap.add_argument("--domain", default="", help="只跑指定 domain")
    ap.add_argument("--case", default="", help="只跑 ID 含该子串的用例")
    ap.add_argument("--tag", default="", help="只跑带该 tag 的用例")
    ap.add_argument("--only-auto", action="store_true", help="只跑自动化用例")
    ap.add_argument("--no-report", action="store_true", help="不写报告文件")
    ap.add_argument("--serial-cases", action="store_true",
                    help="包含串口用例（会复位设备，排在最末执行）")
    ap.add_argument("--list", action="store_true", help="仅列出匹配用例，不执行")
    ap.add_argument("--no-discover", dest="discover", action="store_const", const="none",
                    help="关闭设备信息自动发现")
    args = ap.parse_args()

    n = runmod.load_cases("cases")
    if n == 0:
        print("未注册任何用例（cases/ 为空？）", file=sys.stderr)
        return 2

    cases = reg.select(domain=args.domain or None, case_id=args.case or None,
                       only_auto=args.only_auto, tag=args.tag or None)

    include_serial = bool(args.serial_cases or args.case or (args.tag == "serial"))

    if args.list:
        for c in cases:
            print(f"{c.id:34s} {c.feature:6s} {c.domain:9s} {c.channel:7s} {c.title}")
        print(f"\n共 {len(cases)} 条")
        return 0

    print("== deep-dog 自动化测试 ==")
    cfg = cfgmod.build(args)
    print(cfg.summary())
    print(f"匹配用例：{len(cases)}\n")

    report = Report(cfg, argv=["python", "run.py"] + sys.argv[1:])
    runmod.run(cases, cfg, report, verbose=True, include_serial=include_serial)

    c = report.counts()
    print(f"\n=== 汇总 === PASS={c['PASS']} FAIL={c['FAIL']} "
          f"SKIP={c['SKIP']} BLOCKED={c['BLOCKED']} ERROR={c['ERROR']}")

    if not args.no_report:
        os.makedirs(REPORT_DIR, exist_ok=True)
        with open(REPORT_MD, "w", encoding="utf-8") as f:
            f.write(report.to_markdown())
        with open(REPORT_JSON, "w", encoding="utf-8") as f:
            f.write(report.to_json())
        print(f"报告：{REPORT_MD}")

    return 0 if c["FAIL"] == 0 and c["ERROR"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
