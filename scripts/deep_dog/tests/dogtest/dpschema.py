"""数据点 schema / 归一化工具命名推导（供 dp、mcp 域用例共用）。

契约：`docs/interface/05-mcp-tool-normalization.md` —— 每个 `channels` 含 `mcp` 的数据点
自动暴露 `self.<module>.<get|set>_<point>`（`access` 为 `wo` 时无 get、`ro` 时无 set）。
"""

from __future__ import annotations

from typing import Any, Optional


def read_schema(ctx) -> Optional[dict]:
    """`GET /api/datapoints/schema` → `data`（失败返回 None）。"""
    try:
        code, body = ctx.http.get_json("/api/datapoints/schema")
    except Exception:
        return None
    if code != 200 or not isinstance(body, dict):
        return None
    data = body.get("data")
    return data if isinstance(data, dict) else None


def expected_tools(schema: dict) -> list[str]:
    """按 schema 推导应自动注册的 MCP 工具名。"""
    out: list[str] = []
    modules = schema.get("modules") or {}
    for mod, mv in modules.items():
        for name, pv in ((mv or {}).get("points") or {}).items():
            if "mcp" not in ((pv or {}).get("channels") or []):
                continue
            access = (pv or {}).get("access", "rw")
            suffix = name.replace(".", "_")
            if access != "wo":
                out.append(f"self.{mod}.get_{suffix}")
            if access != "ro":
                out.append(f"self.{mod}.set_{suffix}")
    return out


def largest_internal_bytes(ctx) -> Any:
    """读 `system.largest_free_internal`（失败返回 None）。

    自动工具常驻 internal 堆，注册下限见 firmware `ws-mcp/datapoint_mcp.cc`
    （`kMinLargestInternalBeforeAdd = 6144`）。
    """
    try:
        code, body = ctx.http.get_json("/api/datapoints/point/system.largest_free_internal")
    except Exception:
        return None
    if code != 200 or not isinstance(body, dict):
        return None
    data = body.get("data")
    if isinstance(data, dict):
        return data.get("value")
    return None
