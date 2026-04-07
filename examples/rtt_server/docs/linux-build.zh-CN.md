[English](linux-build.md) · [整体架构总览](architecture.zh-CN.md) · [返回 README](../README_ZN.md)

# Linux 编译与运行文档

本文档覆盖 `client_demo/` 的 **Linux SocketCAN** 路径。

除非特别说明，本文档中的运行示例统一遵循下面这条命令模板：

```bash
./client -i can1 -s 7D1 -t 7E1 -f 7E0
```

在当前文档约定里，Linux 默认后端就是 `socketcan`，因此示例命令保持最简写法。

## 适用范围

当前代码树中支持以下 Linux 构建方式：

- 原生 CMake 构建
- 使用 `toolchain.cmake` 的交叉构建
- 基于历史 `Makefile` 的兼容构建
- 可选的远程部署辅助目标 `download`

## 为什么 Linux 走这条路径

仓库中的 Linux 路径刻意保持简洁：

- `client_demo/CMakeLists.txt` 在 Linux 下选择 `transport_socketcan.c`
- `transport_socketcan.c` 将 UDS client 直接绑定到平台原生 ISO-TP socket 后端
- `core/client_shell.c` + `utils/linenoise.c` 提供交互 shell，无需再写第二套终端子系统

这样 Linux 构建产物就是一个 **单体原生 C 可执行文件**。

## 前置条件

- Linux 主机或 Linux 构建容器
- 被 SocketCAN 支持的 CAN 接口
- 系统头文件中包含 `linux/can/isotp.h`
- CMake 3.16+（用于 CMake 路径）
- GCC / Clang 工具链

## 构建方式

### 方式 A：原生 CMake 构建

```bash
cd client_demo
mkdir -p build
cd build
cmake ..
cmake --build . -j
```

生成文件：

```text
client_demo/build/client
```

### 方式 B：兼容 Makefile 构建

```bash
cd client_demo
make
```

生成文件：

```text
client_demo/client
```

### 方式 C：使用仓库自带 toolchain 文件做交叉构建

```bash
cd client_demo
mkdir -p build-cross
cd build-cross
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake
cmake --build . -j
```

## SocketCAN 准备

真实 CAN 接口示例：

```bash
sudo ip link set can1 down || true
sudo ip link set can1 type can bitrate 1000000
sudo ip link set can1 up
```

如果你要做本地协议 smoke，可以自行创建虚拟 CAN 设备，但客户端运行参数形态仍建议保持为下文这一组。

## 运行示例

### 标准 Linux 运行命令

```bash
./client -i can1 -s 7D1 -t 7E1 -f 7E0
```

### 显式指定后端的写法

```bash
./client -b socketcan -i can1 -s 7D1 -t 7E1 -f 7E0
```

## 可选的远程部署辅助目标

当你启用 `ENABLE_DOWNLOAD` 后，CMake 会暴露一个 `download` 目标，用于构建、上传并在远端目标机上执行该二进制。

示例：

```bash
cd client_demo/build
cmake .. -DENABLE_DOWNLOAD=ON
cmake --build . --target download
```

关键 cache 变量包括：

- `TARGET_IP`
- `TARGET_USER`
- `TARGET_DIR`
- `TARGET_BIN_NAME`
- `DOWNLOAD_CLIENT_IF`
- `DOWNLOAD_CLIENT_SA`
- `DOWNLOAD_CLIENT_TA`
- `DOWNLOAD_CLIENT_FA`

## 为什么 Linux 选择宿主机 ISO-TP socket

Linux 路径是围绕宿主操作系统设计的，而不是在用户态再复制一套 transport 协议栈。

这样做有三个直接好处：

1. 构建保持为纯 C
2. Linux 宿主本身已经能把 CAN 接口暴露成 SocketCAN 设备
3. transport 后端可以保持很薄，只负责把 UDS 绑定到内核 transport 路径

## 常见问题

### 缺少 `linux/can/isotp.h`

Linux CMake 路径会在缺少该头文件时直接失败。

常见原因：

- 宿主机头文件不完整
- 交叉编译 sysroot 未包含 Linux CAN ISO-TP UAPI 头文件

### 能编译，但运行时打不开接口

请检查：

- `-i` 传入的接口名是否正确
- 接口是否已经 `UP`
- 波特率和总线实际配置是否匹配

### ECU 没有响应

请检查：

- `-s`、`-t`、`-f` 的地址值
- 当前链路是物理寻址还是功能寻址
- 目标 ECU 是否已经处于预期诊断会话中

## 相关文档

- [整体架构总览](architecture.zh-CN.md)
- [API 参考](api-reference.zh-CN.md)
- [Windows 编译文档](windows-build.zh-CN.md)
- [服务端模块说明](../server_demo/README_ZN.md)
