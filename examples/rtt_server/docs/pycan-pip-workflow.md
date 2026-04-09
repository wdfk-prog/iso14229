[中文](pycan-pip-workflow.zh-CN.md) · [Windows Build Guide](windows-build.md) · [Back to README](../README.md)

# Python / pip Workflow for `PYCAN_BRIDGE`

This document explains the **Python-side dependency flow** for the Windows `PYCAN_BRIDGE` backend: from installation, to package purpose, to runtime execution.

The client command examples in this page keep the same shared runtime prefix:

```powershell
.\client_demo\build-mingw64\client.exe -i can1 -s 7D1 -t 7E1 -f 7E0
```

The backend-specific `pycan_bridge` options are appended after that shared block.

## 1. What this document solves

The Windows path is not just “install Python and run a script”. It requires a repeatable Python-side environment for:

- `client_demo/tools/pycan_bridge.py`
- `client_demo/tools/pycan_runtime.py`
- adapter access through `python-can`

## 2. Recommended workflow

### 2.1 Create a dedicated virtual environment

From the repository root in PowerShell:

```powershell
py -3 -m venv .venv
```

### 2.2 Upgrade pip inside that environment

```powershell
.\.venv\Scripts\python -m pip install -U pip
```

### 2.3 Install the bridge dependencies

```powershell
.\.venv\Scripts\python -m pip install -r client_demo\tools\requirements-pycan.txt
```

## 3. Why these packages are listed

Current file: `client_demo/tools/requirements-pycan.txt`

### `python-can`

Core CAN abstraction package used by the sidecar.

The repository needs it because:

- `pycan_bridge.py` opens the CAN bus through `python-can`
- the bridge wants one user-facing API while keeping adapter specifics below it

### `pyserial`

Required for the `slcan` path.

The repository needs it because:

- `slcan` devices are accessed through serial / COM-port style endpoints
- `COM4@9600` style channels depend on a serial transport layer

### `pyusb`

Required for the `gs_usb` path.

The repository needs it because:

- `gs_usb` adapters are USB devices
- the Python stack needs USB access on the host side

### `gs-usb`

Required for the `gs_usb` adapter family.

The repository needs it because:

- `python-can` integrates with the `gs_usb` adapter ecosystem
- the bridge validates and opens the `gs_usb` interface explicitly

### `libusb-package` (Windows only)

Windows helper for the USB backend path.

The repository needs it because:

- the Windows `gs_usb` path must make sure a usable libusb backend can actually be found from Python
- `pycan_runtime.py` contains explicit logic to attach that backend when opening `gs_usb`

## 4. Why the documentation links to Python and pip

These links are part of the repository execution model, not generic beginner filler.

- Python install docs are needed because the sidecar is a Python process.
- pip docs are needed because the sidecar dependencies must be installed into the same venv that launches the bridge.
- Windows build docs are needed because the formal client binary is still compiled separately by MSYS2.

Official links:

- [Python on Windows](https://docs.python.org/3/using/windows.html)
- [Python Releases for Windows](https://www.python.org/downloads/windows/)
- [pip Installation Guide](https://pip.pypa.io/en/stable/installation/)

## 5. `gs_usb` vs `slcan`

### `gs_usb`

Choose this when the adapter is a USB CAN device supported by the `gs_usb` ecosystem.

Example run:

```powershell
.\client_demo\build-mingw64\client.exe -i can1 -s 7D1 -t 7E1 -f 7E0 -b pycan_bridge --python .\.venv\Scripts\python.exe --bridge-script client_demo\tools\pycan_bridge.py --py-if gs_usb --py-channel 0 --bitrate 1000000
```

### `slcan`

Choose this when the adapter is presented as a serial / COM-port endpoint.

Example run:

```powershell
.\client_demo\build-mingw64\client.exe -i can1 -s 7D1 -t 7E1 -f 7E0 -b pycan_bridge --python .\.venv\Scripts\python.exe --bridge-script client_demo\tools\pycan_bridge.py --py-if slcan --py-channel COM4@9600 --bitrate 1000000
```

### Difference summary

| Interface | Strength | Caveat |
| --- | --- | --- |
| `gs_usb` | direct USB-oriented CAN adapter path | depends on correct USB backend / driver availability |
| `slcan` | simple serial / COM-port fallback | limited by serial-style adapter behavior |

## 6. Why this Python path is usually slower than Linux

This document focuses on `PYCAN_BRIDGE`. Once you choose this path, it means:

- the C client does not talk to the adapter directly
- raw CAN frames move back and forth between C and Python through the bridge
- `python-can` then forwards those frames to the `gs_usb` or `slcan` backend

Compared with the native Linux ISO-TP socket path, the default Windows implementation therefore usually adds:

- **process-switch overhead**
- **packet framing and metadata encode/decode overhead**
- **pipe buffering and flush latency**
- **user-space USB / serial adapter overhead**

If `slcan` is used, the stack also adds a serial ASCII interaction layer, so it is typically heavier than `gs_usb`.

## 7. Verify the environment before running the client

### Check Python location

```powershell
.\.venv\Scripts\python -c "import sys; print(sys.executable)"
```

### Check `python-can`

```powershell
.\.venv\Scripts\python -c "import can; print(can.__version__)"
```

### Check the bridge imports

```powershell
.\.venv\Scripts\python client_demo\tools\pycan_bridge.py --help
```

## 8. End-to-end run order

1. install Python
2. create `.venv`
3. upgrade pip in `.venv`
4. install `requirements-pycan.txt`
5. build `client.exe` with MSYS2 MINGW64
6. run `client.exe` and point `--python` at `.venv\Scripts\python.exe`

## 9. Common problems

### `python-can is not installed`

You are likely launching one Python interpreter and installing packages into another.

Fix: install packages using the same interpreter passed to `--python`.

### `gs_usb` cannot access the device

Check the host USB driver path and confirm that the Python environment can actually see the USB backend.

### `slcan` cannot open the COM port

Check the channel string format and serial baud encoding, for example `COM4@9600`.

## Related documents

- [Windows Build Guide](windows-build.md)
- [Architecture Overview](architecture.md)
- [API Reference](api-reference.md)
