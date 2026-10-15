# deep-dog：本机 ESP-IDF 编译/烧录环境（Windows）

在本机（Windows）编译/烧录 `xiaozhi-esp32`（deep-dog 板）固件前，**必须先激活 ESP-IDF 环境**，
否则 `idf.py` 不可用。

## 激活方式

ESP-IDF 安装在 `D:\working\dev\esp-idf`，PowerShell profile
（`C:\Users\<user>\Documents\WindowsPowerShell\Microsoft.PowerShell_profile.ps1`）已定义函数：

```powershell
get_idf55
```

等价于：

```powershell
$env:IDF_PATH = 'D:\working\dev\esp-idf'
. "$env:IDF_PATH\export.ps1"
```

> **重要**：非交互式 shell（脚本 / Agent 工具调用）默认**不加载 profile**，`get_idf55` 不可用，
> 必须显式 dot-source `export.ps1`：
> ```powershell
> $env:IDF_PATH='D:\working\dev\esp-idf'; . "$env:IDF_PATH\export.ps1"
> ```

激活成功标志：输出 `ESP-IDF v5.5.2`。

## 编译 / 烧录

```powershell
cd D:\working\test-platform\xiaozhi-esp32
idf.py build                    # 增量编译
idf.py -p COM6 flash            # 烧录（本机 USB 串行设备为 COM6）
idf.py -p COM6 monitor          # 串口监视（Ctrl+] 退出）
```

首次或切换芯片时先 `idf.py set-target esp32s3`。

## 常见问题

| 现象 | 处理 |
|------|------|
| `idf.py` 不是命令 | 未激活环境；执行上面的 dot-source |
| 串口不确定 | `Get-CimInstance Win32_SerialPort`；本机 USB 串行为 `COM6` |
| 环境串味（conda 干扰） | export.ps1 会重建 PATH；如仍冲突，先 `conda deactivate` |

## 备注

- macOS 侧开发用 `get_idf55` / `get_idf60`（见根 `AGENTS.md`），IDF 路径为 `/Volumes/MacExtStorage/projects/...`。
- 本机 Windows 为**编译/烧录可用**环境；deep-dog 板级代码优先在 `main/boards/deep-dog/`。
