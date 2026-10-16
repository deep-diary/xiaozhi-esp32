# LL-007：IDE 编辑工具超时后改动其实已落盘，重试会造成重复写入

- **编号**：LL-007
- **日期**：2026-10-15
- **关键词**：Edit 超时 / IDE Command timeout / redefinition / 重复应用 / 重试前先读

## 现象

编辑工具返回失败：

```
unexpect error: IDE Command timeout: Elapsed(())
```

于是按惯例**重试同一个编辑**。结果代码里出现**两份相同的新增内容**，例如：

```
error: redefinition of 'bool dp_servo_write_target(void*, int32_t)'
warning: 'bool dp_servo_write_target(void*, int32_t)' defined but not used
```

## 根因

该类超时是**响应读取超时**，不是**执行失败**——写入其实已经落盘。
重试等价于把同一段 diff 再打一遍，于是新增块被插入两次；
若 old_string 是"锚点行"，就会锚点前后各插一份，产生重复定义。

## 正确做法

1. **超时后不要立刻重试**，先用 `Read` 查看目标行段，确认改动是否已生效。
2. 已生效 → **不要再重试**，继续后续步骤。
3. 确需重试 → 把 `old_string` 换成**包含新增内容的更宽上下文**，
   使其仅在"未应用"时才匹配，避免二次插入。
4. 发现重复后：删除多余那份（保留一份），重新编译确认；
   C++ 侧典型报错是 `error: redefinition of ...` / `-Wunused-function`。

## 相关文件

- 本次触发点：`main/boards/deep-dog/mqtt/modules/servo_datapoints.cc`（`servo.target` 改可写时）
