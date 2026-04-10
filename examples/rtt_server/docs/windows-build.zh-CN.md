[English](windows-build.md) · [整体架构总览](architecture.zh-CN.md) · [返回 README](../README_ZN.md)

# Windows 编译文档

本文档覆盖 `client_demo/` 的 **Windows / MSYS2 MINGW64** 构建路径。

所有运行示例都先保留同一组诊断参数前缀：

```powershell
.\client_demo\build-mingw64\client.exe -i can1 -s 7D1 -t 7E1 -f 7E0
```

在 Windows 下，真正的适配器打开方式仍由 `--py-if` 和 `--py-channel` 决定；这里保留统一的 `-i can1` 前缀，是为了让跨平台文档示例保持一致。

## 适用范围

当前仓库中的 Windows 相关后端主要有两种：

- **推荐路径**：`PYCAN_BRIDGE`
- **历史 smoke 路径**：`TSMASTER_API`

当前正式文档已经把 **`PYCAN_BRIDGE` 作为默认 Windows 路径**。

## 官方安装教程链接（仅链接）

- [Python on Windows](https://docs.python.org/3/using/windows.html)
- [Python Releases for Windows](https://www.python.org/downloads/windows/)
- [MSYS2 Installer Guide](https://www.msys2.org/docs/installer/)
- [pip Installation Guide](https://pip.pypa.io/en/stable/installation/)

## 为什么 Windows 要采用这套环境模型

仓库刻意把 Windows 工作拆成两层：

- **MSYS2 MINGW64** 负责构建 C 可执行文件
- **Windows Python** 负责创建虚拟环境并运行 Python CAN sidecar

这种拆分可以让正式客户端继续保持为 C 构建产物，同时避免把仓库硬绑定到单一厂商 CAN SDK。

## 为什么默认选 `PYCAN_BRIDGE`

以下场景建议使用 `PYCAN_BRIDGE`：

- 你要走仓库当前的主线路径
- 你的适配器可以被 `python-can` 访问
- 你希望 Python 侧依赖通过 venv 管理，而不是依赖固定 SDK

以下场景才建议使用 `TSMASTER_API`：

- 你的环境已经绑定 TSMaster
- 你已经具备匹配的 SDK 头文件与导入库
- 你只需要 smoke / 历史验证路径

## 1. 在 MSYS2 MINGW64 中安装构建工具

打开 **MSYS2 MINGW64** shell，安装以下包：

```bash
pacman -Syu
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
```

## 2. 构建推荐的 `PYCAN_BRIDGE` 客户端

在 **MINGW64** shell 中执行：

```bash
cd client_demo
cmake --preset windows-pycan-mingw64
cmake --build --preset build-client-pycan
```

生成的可执行文件：

```text
client_demo/build-mingw64/client.exe
```

## 3. 从源码树运行时，为 sidecar 创建 Python 环境

在仓库根目录的 PowerShell 中执行：

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python -m pip install -U pip
.\.venv\Scripts\python -m pip install -r client_demo\tools\requirements-pycan.txt
```

每个包为什么要装，请继续看 [Python / pip 工作流](pycan-pip-workflow.zh-CN.md)。

## 4. 从源码树运行 Windows 客户端

### 从源码树运行的 `gs_usb` 示例

```powershell
.\client_demo\build-mingw64\client.exe -i can1 -s 7D1 -t 7E1 -f 7E0 -b pycan_bridge --python .\.venv\Scripts\python.exe --bridge-script client_demo\tools\pycan_bridge.py --py-if gs_usb --py-channel 0 --bitrate 1000000
```

### 从源码树运行的 `slcan` 示例

```powershell
.\client_demo\build-mingw64\client.exe -i can1 -s 7D1 -t 7E1 -f 7E0 -b pycan_bridge --python .\.venv\Scripts\python.exe --bridge-script client_demo\tools\pycan_bridge.py --py-if slcan --py-channel COM4@9600 --bitrate 1000000
```

## 5. 运行下载得到的 Windows 发布包

如果你下载的是预构建好的 **Windows release ZIP**，解压后直接在发布包根目录运行即可。

当前发布包已经内置：

- `client.exe`
- `python.exe`
- 嵌入式 Python 运行时文件以及 `python*._pth`
- 放在 `runtime/pyd` 下的标准库扩展模块
- 放在 `Lib/site-packages` 下的 Python bridge 依赖
- `tools/pycan_bridge.py` 与 `tools/pycan_runtime.py`

因此下载得到的发布包**不需要**：

- 额外在系统里安装 Python
- 手工传 `--python`
- 手工传 `--bridge-script`

### 发布包的标准命令

#### `gs_usb`

```powershell
.\client.exe -i can1 -s 7D1 -t 7E1 -f 7E0 -b pycan_bridge --py-if gs_usb --py-channel 0 --bitrate 1000000
```

#### `slcan`

```powershell
.\client.exe -i can1 -s 7D1 -t 7E1 -f 7E0 -b pycan_bridge --py-if slcan --py-channel COM4@9600 --bitrate 1000000
```

### 下载版 `client.exe` 参数说明

| 参数 | 含义 | 文档中的典型值 |
| --- | --- | --- |
| `-i` | shell / transport 配置中使用的逻辑接口名 | `can1` |
| `-s` | tester 物理源地址 | `7D1` |
| `-t` | ECU 物理目标地址 | `7E1` |
| `-f` | 功能寻址目标地址 | `7E0` |
| `-b` | transport 后端 | `pycan_bridge` |
| `--py-if` | Python bridge 选择的适配器族 | `gs_usb` 或 `slcan` |
| `--py-channel` | 传给所选 Python bridge 后端的通道标识 | `0` 或 `COM4@9600` |
| `--bitrate` | CAN 标称波特率 | `1000000` |

### 如何配置 Python bridge 参数

- `gs_usb` 类 USB CAN 适配器使用 `--py-if gs_usb --py-channel 0`。如果同时接了多个设备，再按实际枚举顺序调整通道号。
- `slcan` / LAWICEL 风格串口适配器使用 `--py-if slcan --py-channel COMx@baud`，例如 `COM4@9600`。
- `-s`、`-t`、`-f` 需要与你的 ECU / tester 寻址配置保持一致。
- `--bitrate` 需要与实际 CAN 总线波特率一致。

### 怎样判断发布包已经启动成功

如果便携运行时已经工作，启动日志至少会走到：

```text
[Context] UDS Context Initialized (backend=pycan_bridge if=...)
```

如果后面出现 `NRC: 0xFF` 之类的负响应，那已经属于发布包启动之后的 ECU / 会话 / 服务侧问题，而不是 Python 运行时打包失败。

## 6. 当前 Windows 文档里支持哪些 CAN 硬件

按照当前仓库文档和 CLI 参数校验，Windows 侧目前只明确支持 **三类硬件路径**：

1. 通过 `PYCAN_BRIDGE` 使用的 **`gs_usb` 类 USB CAN 适配器**
   - 例如：candleLight / CANable / canlight 风格设备，或其他能被 `gs_usb` 栈识别的适配器
   - 典型参数：`--py-if gs_usb --py-channel 0`
2. 通过 `PYCAN_BRIDGE` 使用的 **`slcan` / LAWICEL 风格串口 CAN 适配器**
   - 例如：在 Windows 下表现为 `COM` 口，并通过 SLCAN 兼容串口协议访问的设备
   - 典型参数：`--py-if slcan --py-channel COM4@9600`
3. 通过历史 `TSMASTER_API` smoke 路径访问的 **TSMaster SDK 绑定适配器**
   - 只适用于已经具备匹配 TSMaster SDK 目录与库文件的环境
   - 这 **不是** 当前默认 Windows 路径

### 重要边界说明

虽然 `python-can` 本身还能支持更多接口族，但**本仓库当前默认 Windows 路径只校验并文档化了 `gs_usb` 和 `slcan`**。也就是说，当前 CLI 会拒绝其他 `--py-if` 取值，因此像 PCAN、Vector、Kvaser、ZLG、Canalyst 这类适配器，**在当前仓库里不应直接宣称为已支持**，除非后续先扩展代码路径。

## 7. `gs_usb` 和 `slcan` 的区别

| 接口 | 含义 | 典型 channel 示例 | 适用场景 |
| --- | --- | --- | --- |
| `gs_usb` | 被 `gs_usb` 生态支持的 USB CAN 适配器 | `0` | candleLight / canable 这类 USB 设备 |
| `slcan` | 基于串口 / COM 口的 SLCAN / LAWICEL 适配器 | `COM4@9600` | 串口设备或 USB 转串口 CAN bridge |

### 实际建议

如果设备能被识别且适配，优先选 `gs_usb`。

如果设备表现为 COM 口，或厂家文档明确写的是 SLCAN 兼容串口协议，就选 `slcan`。

## 8. 为什么 Windows 不能直接复用 Linux 的终端实现

Windows 构建会编译：

- `core/client_shell_windows.c`
- `platform/platform_windows.c`

而不是直接拿 Linux 的 `linenoise` 路径复用。

这是有意的，因为 Windows console 侧必须处理：

- console mode 的保存 / 恢复
- `_kbhit()` / `_getch()` 输入轮询
- VT 能力检测
- 不支持 VT 时的降级重绘逻辑

所以项目不是简单复用 Linux shell，而是在保持命令模型一致的前提下，为 Windows 提供了独立 console 实现。

## 9. 为什么 Windows 一般会比 Linux 更慢

在这个仓库里，Windows 客户端通常会比 Linux 路径多一层甚至多层软件栈。

Linux 推荐路径：

`client -> transport_socketcan -> Linux ISO-TP socket -> ECU`

Windows 推荐路径：

`client -> transport_pycan_bridge -> stdio 包 IPC -> pycan_bridge.py -> python-can -> gs_usb/slcan -> ECU`

所以 Windows 端常见的额外开销来自：

- **多一个 Python sidecar 进程**
- **C/Python 之间的包封装与元数据序列化 / 反序列化**
- **子进程管道与 stdio 刷新**
- **`python-can` 适配层本身的用户态开销**
- **若走 `slcan`，还会叠加串口 ASCII 协议开销**

因此这里更准确的说法是：**不是 Windows 系统一定慢，而是当前仓库的 Windows 默认实现链路更长。**

### 实际建议

- 能用 `gs_usb` 就尽量不要走 `slcan`
- 尽量让 client 与 bridge 在同一台机器上本地运行
- 如果你主要做高频交互或时延敏感调试，优先选择 Linux 主机路径

## 10. 构建历史 `TSMASTER_API` smoke 目标

只有在你已经具备匹配 TSMaster SDK 的前提下，才建议使用这一条路径。

### 使用环境变量的 preset

```bash
export UDS_TSMASTER_SDK_DIR='D:/TOSUN/TSMaster/Data/SDK'
cmake --preset windows-tsmaster-mingw64
cmake --build --preset build-client-smoke
```

### 使用内联 SDK 路径的 preset

```bash
cmake --preset windows-tsmaster-mingw64-inline-sdk
cmake --build --preset build-client-smoke-inline-sdk
```

## 11. 常见问题

### CMake 提示 Windows 支持未启用

请使用仓库自带的 Windows preset。该路径要求 `-DUDS_ENABLE_WINDOWS=ON`，而 preset 已经处理好了。

### 客户端能启动，但 bridge 打不开适配器

请检查：

- `--python` 指向的解释器
- `--bridge-script` 路径
- `--py-if` 与 `--py-channel`
- 所需 Python 包是否安装在同一个 venv 里

### `gs_usb` 在 Windows 下失败

请检查 Python 依赖环境和宿主机 USB 驱动状态。

### `slcan` 打不开

请检查 COM 口字符串格式，必要时把串口波特率也写进 channel，例如 `COM4@9600`。

## 相关文档

- [整体架构总览](architecture.zh-CN.md)
- [API 参考](api-reference.zh-CN.md)
- [Python / pip 工作流](pycan-pip-workflow.zh-CN.md)
- [服务端模块说明](../server_demo/README_ZN.md)
