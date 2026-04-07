[中文](windows-build.zh-CN.md) · [Architecture Overview](architecture.md) · [Back to README](../README.md)

# Windows Build Guide

This document covers the **Windows / MSYS2 MINGW64** build path for `client_demo/`.

All runtime examples keep the same diagnostic-address block first:

```powershell
.\client_demo\build-mingw64\client.exe -i can1 -s 7D1 -t 7E1 -f 7E0
```

On Windows, the actual adapter opening still depends on `--py-if` and `--py-channel`; the shared `-i can1` prefix is kept so the documentation stays aligned across platforms.

## Scope

The repository currently exposes two Windows-oriented backend choices:

- **Recommended**: `PYCAN_BRIDGE`
- **Legacy smoke path**: `TSMASTER_API`

The formal documentation now treats **`PYCAN_BRIDGE` as the default Windows path**.

## Official installation tutorials (links only)

- [Python on Windows](https://docs.python.org/3/using/windows.html)
- [Python Releases for Windows](https://www.python.org/downloads/windows/)
- [MSYS2 Installer Guide](https://www.msys2.org/docs/installer/)
- [pip Installation Guide](https://pip.pypa.io/en/stable/installation/)

## Why Windows uses this environment model

The repository deliberately splits Windows work into two layers:

- **MSYS2 MINGW64** builds the C executable
- **Windows Python** creates the venv and runs the Python CAN sidecar

This separation keeps the formal client build in C while avoiding a hard dependency on one vendor-specific CAN SDK.

## Why `PYCAN_BRIDGE` is the default

Use `PYCAN_BRIDGE` when:

- you want the repository’s current primary Windows path
- your adapter is reachable through `python-can`
- you want a venv-managed Python sidecar instead of an SDK-coupled CAN layer

Use `TSMASTER_API` only when:

- your environment is already tied to TSMaster
- you already have the matching SDK headers and import library
- you only need the smoke / legacy validation path

## 1. Install build tools in MSYS2 MINGW64

Open the **MSYS2 MINGW64** shell and install the required packages:

```bash
pacman -Syu
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
```

## 2. Build the recommended `PYCAN_BRIDGE` client

In the **MINGW64** shell:

```bash
cd client_demo
cmake --preset windows-pycan-mingw64
cmake --build --preset build-client-pycan
```

Generated binary:

```text
client_demo/build-mingw64/client.exe
```

## 3. Create the Python environment for the sidecar

From the repository root in PowerShell:

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python -m pip install -U pip
.\.venv\Scripts\python -m pip install -r client_demo\tools\requirements-pycan.txt
```

See [Python / pip Workflow](pycan-pip-workflow.md) for package-level reasoning.

## 4. Run the Windows client

### `gs_usb` example

```powershell
.\client_demo\build-mingw64\client.exe -i can1 -s 7D1 -t 7E1 -f 7E0 -b pycan_bridge --python .\.venv\Scripts\python.exe --bridge-script client_demo\tools\pycan_bridge.py --py-if gs_usb --py-channel 0 --bitrate 1000000
```

### `slcan` example

```powershell
.\client_demo\build-mingw64\client.exe -i can1 -s 7D1 -t 7E1 -f 7E0 -b pycan_bridge --python .\.venv\Scripts\python.exe --bridge-script client_demo\tools\pycan_bridge.py --py-if slcan --py-channel COM4@9600 --bitrate 1000000
```

## 5. `gs_usb` vs `slcan`

| Interface | What it represents | Typical channel example | When to choose |
| --- | --- | --- | --- |
| `gs_usb` | USB CAN adapters supported by the `gs_usb` ecosystem | `0` | USB dongles such as candleLight / canable style devices |
| `slcan` | serial / COM-port based SLCAN or LAWICEL adapters | `COM4@9600` | serial devices or USB-serial CAN bridges |

### Practical guidance

Choose `gs_usb` first when the adapter is detected and supported.

Choose `slcan` when the device is presented as a COM port or documented as an SLCAN-compatible serial adapter.

## 6. Why the Windows shell is separate from Linux

The Windows build compiles:

- `core/client_shell_windows.c`
- `platform/platform_windows.c`

instead of the Linux `linenoise`-based shell path.

That is deliberate because the Windows console path must manage:

- console mode capture / restore
- `_kbhit()` / `_getch()` polling
- VT capability detection
- fallback redraw behavior

So the project does not reuse the Linux shell blindly. It keeps the command model the same, but uses a Windows-specific console implementation.

## 7. Why Windows is usually slower than Linux

In this repository, the Windows client usually carries a longer software path than the Linux path.

Recommended Linux path:

`client -> transport_socketcan -> Linux ISO-TP socket -> ECU`

Recommended Windows path:

`client -> transport_pycan_bridge -> stdio JSONL -> pycan_bridge.py -> python-can -> gs_usb/slcan -> ECU`

That means the Windows side commonly adds overhead from:

- **an extra Python sidecar process**
- **JSON Lines serialization and parsing between C and Python**
- **child-process pipes and stdio flushing**
- **the user-space `python-can` adapter layer itself**
- **serial ASCII protocol overhead when `slcan` is used**

So the accurate statement is: **Windows is not automatically slower as an operating system; the default Windows implementation in this repository is usually slower because its runtime chain is longer.**

### Practical guidance

- Prefer `gs_usb` over `slcan` when your hardware supports it.
- Keep the client and bridge on the same machine.
- Prefer the Linux host path for high-frequency or latency-sensitive diagnostic work.

## 8. Build the legacy `TSMASTER_API` smoke target

Use this only when the matching TSMaster SDK is already available.

### Environment-variable driven preset

```bash
export UDS_TSMASTER_SDK_DIR='D:/TOSUN/TSMaster/Data/SDK'
cmake --preset windows-tsmaster-mingw64
cmake --build --preset build-client-smoke
```

### Inline SDK preset

```bash
cmake --preset windows-tsmaster-mingw64-inline-sdk
cmake --build --preset build-client-smoke-inline-sdk
```

## 9. Common problems

### CMake says Windows support is disabled

Use the provided Windows preset. The Windows branch requires `-DUDS_ENABLE_WINDOWS=ON`, which the preset already supplies.

### The client starts but the bridge cannot open the adapter

Check:

- the Python executable passed by `--python`
- the bridge path passed by `--bridge-script`
- `--py-if` and `--py-channel`
- whether the required Python packages were installed into the same venv

### `gs_usb` fails on Windows

Check the Python-side dependency environment and host USB driver state.

### `slcan` fails to open

Check the COM port string format and ensure the serial baud is encoded as part of the channel when needed, for example `COM4@9600`.

## Related documents

- [Architecture Overview](architecture.md)
- [API Reference](api-reference.md)
- [Python / pip Workflow](pycan-pip-workflow.md)
- [Server Module Notes](../server_demo/README.md)
