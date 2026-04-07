[English](pycan-pip-workflow.md) · [Windows 编译文档](windows-build.zh-CN.md) · [返回 README](../README_ZN.md)

# `PYCAN_BRIDGE` 的 Python / pip 工作流

本文档里的客户端示例也统一沿用这组运行参数前缀：

```powershell
.\client_demo\build-mingw64\client.exe -i can1 -s 7D1 -t 7E1 -f 7E0
```

在这组统一前缀之后，再追加 `pycan_bridge` 所需的 Python 侧参数。

本文档解释 Windows `PYCAN_BRIDGE` 后端的 **Python 侧依赖链路**：从安装，到每个包的作用，再到运行方式。

## 1. 这份文档解决什么问题

Windows 路径并不是“装个 Python 然后跑脚本”这么简单。它需要一套可复现的 Python 侧环境来支撑：

- `client_demo/tools/pycan_bridge.py`
- `client_demo/tools/pycan_runtime.py`
- 通过 `python-can` 访问 CAN 适配器

## 2. 推荐工作流

### 2.1 创建独立虚拟环境

在仓库根目录的 PowerShell 中执行：

```powershell
py -3 -m venv .venv
```

### 2.2 在该环境中升级 pip

```powershell
.\.venv\Scripts\python -m pip install -U pip
```

### 2.3 安装 bridge 依赖

```powershell
.\.venv\Scripts\python -m pip install -r client_demo\tools\requirements-pycan.txt
```

## 3. 为什么这些包必须存在

当前依赖清单文件：`client_demo/tools/requirements-pycan.txt`

### `python-can`

这是 sidecar 使用的核心 CAN 抽象库。

仓库需要它，因为：

- `pycan_bridge.py` 通过 `python-can` 打开 CAN bus
- bridge 需要一个统一的用户层 API，而把适配器差异留在更底层

### `pyserial`

这是 `slcan` 路径的必要依赖。

仓库需要它，因为：

- `slcan` 设备是通过串口 / COM 口风格访问的
- `COM4@9600` 这类 channel 依赖串口传输层

### `pyusb`

这是 `gs_usb` 路径的必要依赖。

仓库需要它，因为：

- `gs_usb` 适配器本质上是 USB 设备
- Python 侧需要宿主机 USB 访问能力

### `gs-usb`

这是 `gs_usb` 设备族的专用依赖。

仓库需要它，因为：

- `python-can` 与 `gs_usb` 设备生态对接
- 当前 bridge 会显式校验并打开 `gs_usb` 接口

### `libusb-package`（仅 Windows）

这是 Windows 下 USB backend 的辅助包。

仓库需要它，因为：

- Windows 上的 `gs_usb` 路径必须确保 Python 真的能找到可用的 libusb backend
- `pycan_runtime.py` 已经包含显式逻辑，在打开 `gs_usb` 时挂接这个 backend

## 4. 为什么文档里必须给 Python 和 pip 的链接

这些链接是当前仓库执行模型的一部分，不是泛泛而谈的入门补充。

- Python 安装文档：因为 sidecar 本身就是 Python 进程
- pip 文档：因为 sidecar 依赖必须装进启动 bridge 的同一个 venv
- Windows 编译文档：因为正式客户端二进制仍然是由 MSYS2 单独构建的

官方链接：

- [Python on Windows](https://docs.python.org/3/using/windows.html)
- [Python Releases for Windows](https://www.python.org/downloads/windows/)
- [pip Installation Guide](https://pip.pypa.io/en/stable/installation/)

## 5. `gs_usb` 和 `slcan` 的区别

### `gs_usb`

当适配器是被 `gs_usb` 生态支持的 USB CAN 设备时，选它。

运行示例：

```powershell
.\client_demo\build-mingw64\client.exe -i can1 -s 7D1 -t 7E1 -f 7E0 -b pycan_bridge --python .\.venv\Scripts\python.exe --bridge-script client_demo\tools\pycan_bridge.py --py-if gs_usb --py-channel 0 --bitrate 1000000
```

### `slcan`

当适配器表现为串口 / COM 口时，选它。

运行示例：

```powershell
.\client_demo\build-mingw64\client.exe -i can1 -s 7D1 -t 7E1 -f 7E0 -b pycan_bridge --python .\.venv\Scripts\python.exe --bridge-script client_demo\tools\pycan_bridge.py --py-if slcan --py-channel COM4@9600 --bitrate 1000000
```

### 差异总结

| 接口 | 优势 | 注意点 |
| --- | --- | --- |
| `gs_usb` | 面向 USB CAN 适配器的直接路径 | 依赖正确的 USB backend / 驱动状态 |
| `slcan` | 简单的串口 / COM 口兜底路径 | 会受到串口类适配器行为限制 |

## 6. 为什么这条 Python 路径通常会比 Linux 更慢

这份文档关注的是 `PYCAN_BRIDGE`。只要你走这条路径，就意味着：

- C 客户端并不直接碰硬件
- 原始 CAN 帧要通过 bridge 在 C 与 Python 之间来回传递
- `python-can` 再把这些帧发给 `gs_usb` 或 `slcan` 后端

所以和 Linux 的原生 ISO-TP socket 路径相比，Windows 默认实现通常会增加：

- **进程切换**
- **JSON Lines 文本协议开销**
- **管道缓冲与刷新延迟**
- **用户态 USB / 串口适配层开销**

如果再使用 `slcan`，还会额外叠加串口 ASCII 交互链路，因此一般会比 `gs_usb` 更重。

## 7. 运行客户端前先验证环境

### 检查 Python 路径

```powershell
.\.venv\Scripts\python -c "import sys; print(sys.executable)"
```

### 检查 `python-can`

```powershell
.\.venv\Scripts\python -c "import can; print(can.__version__)"
```

### 检查 bridge 是否能导入

```powershell
.\.venv\Scripts\python client_demo\tools\pycan_bridge.py --help
```

## 8. 从安装到运行的顺序

1. 安装 Python
2. 创建 `.venv`
3. 在 `.venv` 中升级 pip
4. 安装 `requirements-pycan.txt`
5. 用 MSYS2 MINGW64 构建 `client.exe`
6. 运行 `client.exe`，并让 `--python` 指向 `.venv\Scripts\python.exe`

## 9. 常见问题

### 提示 `python-can is not installed`

通常说明你启动 bridge 的 Python，与安装依赖的 Python 不是同一个解释器。

修复方式：使用传给 `--python` 的同一个解释器来安装依赖。

### `gs_usb` 无法访问设备

请检查宿主机 USB 驱动路径，并确认 Python 环境真的能看到对应 USB backend。

### `slcan` 打不开 COM 口

请检查 channel 字符串格式，并确认串口波特率已编码进参数，例如 `COM4@9600`。

## 相关文档

- [Windows 编译文档](windows-build.zh-CN.md)
- [整体架构总览](architecture.zh-CN.md)
- [API 参考](api-reference.zh-CN.md)
