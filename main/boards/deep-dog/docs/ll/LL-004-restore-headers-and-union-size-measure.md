# LL-004 还原头文件后增量编译不生效 + 全模式固件体积实测方法

## LL-004a 用 Copy-Item 还原头文件后，idf.py build 不重编（假象）

- **现象**：把 `config.h` / `board_features.h` 用备份还原后，`idf.py build` 只输出
  `[4/4] Completed 'bootloader'` 就结束，`xiaozhi.bin` 大小仍是改动前的值——
  看起来"编译通过"，实际 build 目录里还是**旧配置**的产物。
- **根因**：PowerShell `Copy-Item` 会把**源文件的 LastWriteTime 一起复制**到目标，
  于是还原后的头文件 mtime 比 `build/` 里的 `.obj` 还旧，ninja 判定"无变更"直接跳过。
  （`git checkout` 恢复的文件 mtime 是新时间，不会踩这个坑。）
- **正确做法**：还原后显式刷新 mtime 再编：
  ```powershell
  (Get-Item main\boards\deep-dog\config.h).LastWriteTime = Get-Date
  idf.py build
  # 校验：输出里的 binary size 应回到预期值
  ```
  判据：改回 CAN 单电机剖面后应看到 `xiaozhi.bin binary size 0x54ac60`（±几十字节构建非确定性）。
- **相关文件**：`main/boards/deep-dog/config.h`、`main/boards/deep-dog/board_features.h`、`build/`（ninja 增量状态）

## LL-004b 测「全模式编入」固件体积的可复现方法

- **用途**：评估「一版固件通吃 38/48 各模式」（[CFG-02](../swrs/config/CFG-02-ext-pin-runtime-mode.md)）时，
  需要真实 flash 增量，而不是估算。
- **正确做法**（**测完必须还原**）：
  1. 备份两个头文件：
     ```powershell
     $b="$env:TEMP\deepdog_union_backup"; New-Item -ItemType Directory -Force -Path $b | Out-Null
     Copy-Item main\boards\deep-dog\config.h "$b\config.h" -Force
     Copy-Item main\boards\deep-dog\board_features.h "$b\board_features.h" -Force
     ```
  2. `config.h`：把所有 `_AVAILABLE 0` 改成 `_AVAILABLE 1`（`#if/#else` 两支都为 1，等效"全可用"）。
  3. `board_features.h`：把所有 `DEEP_DOG_*_ENABLE` 默认值改成 `1`
     （`SERVO` 会被 `GIMBAL && SERVO` 互斥强制为 0，符合预期）。
  4. `idf.py build`，读末尾 `check_sizes` 的 `binary size / free`。
  5. 还原备份 → **按 LL-004a 刷新 mtime** → 再 `idf.py build` 校验尺寸回到基线。
- **实测结论（esp32s3 / IDF 5.5 / app 分区 0x5a0000，2026-10）**：
  | 形态 | bin | 余量 |
  |------|-----|------|
  | CAN 单电机剖面 | `0x54ac40`（5,549,120 B） | `0x553c0`（349,120 B，6%） |
  | 全模式编入 | `0x550240`（5,571,136 B） | `0x4fdc0`（327,104 B，6%） |
  | 增量 | **+22,016 B（≈21.5 KB）** | — |
  → 说明 `dog/leg/trajectory/servo/gimbal/led/uart/io_ext/ad/rs485` 都是薄封装，体积不是运行时切模式的障碍。
- **符号校验**（确认真的编进去了，避免"空 TU 假象"）：
  ```powershell
  xtensa-esp32s3-elf-nm -C build\xiaozhi.elf | Select-String 'DogControl::|DeepDogLedInit|DeepDogUartInit|DeepDogIoExtInit|DeepDogAdInit|DeepDogGimbalInit'
  ```
  全模式构建应有匹配；切回单剖面后 `Gimbal::` / `ServoControl::` 应为 0 条。
- **相关文件**：`main/boards/deep-dog/config.h`、`main/boards/deep-dog/board_features.h`、`partitions/v2/`
