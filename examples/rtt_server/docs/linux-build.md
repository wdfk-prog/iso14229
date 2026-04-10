[中文](linux-build.zh-CN.md) · [Architecture Overview](architecture.md) · [Back to README](../README.md)

# Linux Build and Run Guide

This document covers the **Linux SocketCAN** path for `client_demo/`.

Unless stated otherwise, every runtime example in this document follows the same command template:

```bash
./client -i can1 -s 7D1 -t 7E1 -f 7E0
```

`socketcan` is the default Linux backend in the documented path, so the examples keep the command short and stable.

## Scope

Supported build styles in this tree:

- native CMake build
- cross build with `toolchain.cmake`
- compatibility build with the legacy `Makefile`
- optional remote deploy helper target `download`

## Why Linux uses this path

The repository’s Linux path is intentionally simple:

- `client_demo/CMakeLists.txt` selects `transport_socketcan.c` on Linux
- `transport_socketcan.c` binds the UDS client to the platform-native ISO-TP socket backend
- `core/client_shell.c` + `utils/linenoise.c` provide the interactive shell without needing a second terminal subsystem

That keeps the Linux build as a **single native C executable**.

## Release artifact note

The repository CI currently publishes the Linux release artifact from the standard `ubuntu-latest` x86-64 runner path, so the downloadable prebuilt Linux binary should be treated as an **x86-64 (`amd64`) build**.

If your target machine is **ARM / AArch64 / armhf** or any other non-x86-64 Linux architecture, do **not** expect the published Linux release binary to run directly. Build it yourself with the appropriate cross toolchain or on a native target of the same architecture.

## Prerequisites

- Linux host or Linux build container
- CAN interface supported by SocketCAN
- Linux headers that include `linux/can/isotp.h`
- CMake 3.16+ for the CMake path
- GCC / Clang toolchain

## Build options

### Option A: native CMake build

```bash
cd client_demo
mkdir -p build
cd build
cmake ..
cmake --build . -j
```

Generated binary:

```text
client_demo/build/client
```

### Option B: compatibility Makefile

```bash
cd client_demo
make
```

Generated binary:

```text
client_demo/client
```

### Option C: cross build with the provided toolchain file (recommended for ARM targets)

```bash
cd client_demo
mkdir -p build-cross
cd build-cross
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake
cmake --build . -j
```

## SocketCAN preparation

Example setup with a real CAN interface:

```bash
sudo ip link set can1 down || true
sudo ip link set can1 type can bitrate 1000000
sudo ip link set can1 up
```

If you want to do a local protocol smoke with a virtual CAN device, create one yourself and still keep the same argument shape used below.

## Run examples

### Canonical Linux run command

```bash
./client -i can1 -s 7D1 -t 7E1 -f 7E0
```

### Same command with an explicit backend flag

```bash
./client -b socketcan -i can1 -s 7D1 -t 7E1 -f 7E0
```

## Optional deploy helper target

If you enable `ENABLE_DOWNLOAD`, CMake exposes a `download` target that builds, uploads, and runs the binary on a remote target.

Example:

```bash
cd client_demo/build
cmake .. -DENABLE_DOWNLOAD=ON
cmake --build . --target download
```

Key cache variables include:

- `TARGET_IP`
- `TARGET_USER`
- `TARGET_DIR`
- `TARGET_BIN_NAME`
- `DOWNLOAD_CLIENT_IF`
- `DOWNLOAD_CLIENT_SA`
- `DOWNLOAD_CLIENT_TA`
- `DOWNLOAD_CLIENT_FA`

## Why Linux uses the host ISO-TP socket

The Linux path is designed around the host operating system instead of re-creating a second transport stack in user space.

That yields three practical benefits:

1. the build remains entirely in C
2. the Linux host already knows how to expose CAN interfaces as SocketCAN devices
3. the transport backend can stay thin and focus on binding UDS to the kernel transport path

## Common problems

### `linux/can/isotp.h` is missing

The Linux CMake path fails early when this header is unavailable.

Typical causes:

- host headers are incomplete
- cross sysroot does not include Linux CAN ISO-TP UAPI headers

### Build works but runtime cannot open the interface

Check:

- the interface name passed by `-i`
- whether the interface is `UP`
- whether bitrate and wiring match the bus

### No response from the ECU

Check:

- `-s`, `-t`, and `-f` values
- physical vs functional addressing assumptions
- whether the target is already in the expected diagnostic session

## Related documents

- [Architecture Overview](architecture.md)
- [API Reference](api-reference.md)
- [Windows Build Guide](windows-build.md)
- [Server Module Notes](../server_demo/README.md)
