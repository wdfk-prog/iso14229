#!/usr/bin/env python3
"""Shared helpers for python-can smoke and bridge scripts.

This module intentionally delays importing ``python-can`` until runtime paths
that actually need hardware access. That keeps ``--help`` and basic protocol
smoke checks usable even when the dependency is not installed yet.
"""
from __future__ import annotations

import binascii
import json
import socket
import struct
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, Optional

PROTOCOL_VERSION = "pycan-bridge/2"
STDIO_PACKET_MAGIC = b"PYCN"
STDIO_PACKET_HEADER = struct.Struct("<4sII")
STDIO_PACKET_MAX_META = 2048
STDIO_PACKET_MAX_DATA = 64
DEFAULT_PYTHON = "python"
DEFAULT_BRIDGE_SCRIPT = "client_demo/tools/pycan_bridge.py"
SUPPORTED_INTERFACES = {"gs_usb", "slcan"}
GS_USB_VENDOR_ID = 0x1D50
GS_USB_PRODUCT_ID = 0x606F


class BridgeConfigError(RuntimeError):
    """Raised for invalid bridge configuration or unsupported feature use."""


class DependencyError(RuntimeError):
    """Raised when optional Python dependencies are missing."""


class ProtocolError(RuntimeError):
    """Raised when incoming protocol messages are malformed."""


@dataclass(frozen=True)
class BusOpenOptions:
    interface_name: str
    channel_name: str
    bitrate: int
    use_canfd: bool = False
    use_brs: bool = False


class JsonLineWriter:
    """Thread-safe JSON Lines writer."""

    def __init__(self, stream, *, owns_stream: bool = False) -> None:
        self._stream = stream
        self._owns_stream = owns_stream
        self._lock = threading.Lock()

    def write(self, payload: Dict[str, Any], data: bytes = b"") -> None:
        wire_payload = dict(payload)
        if data:
            wire_payload["data"] = hex_from_bytes(data)
        line = json.dumps(wire_payload, ensure_ascii=False, separators=(",", ":")) + "\n"
        with self._lock:
            if self._stream is None:
                raise BrokenPipeError("writer stream is already closed")
            if hasattr(self._stream, "sendall"):
                self._stream.sendall(line.encode("utf-8"))
            else:
                self._stream.write(line)
                self._stream.flush()

    def close(self, *, abort: bool = False) -> None:
        with self._lock:
            stream = self._stream
            self._stream = None

        if stream is None or not self._owns_stream:
            return

        if hasattr(stream, "sendall"):
            if abort:
                try:
                    stream.shutdown(socket.SHUT_RDWR)
                except Exception:
                    pass
            try:
                stream.close()
            except Exception:
                pass
            return

        if abort and hasattr(stream, "detach"):
            try:
                detached = stream.detach()
            except Exception:
                detached = None
            if detached is not None:
                try:
                    detached.close()
                except Exception:
                    pass
            return

        try:
            stream.flush()
        except Exception:
            pass
        try:
            stream.close()
        except Exception:
            pass


class BinaryPacketWriter:
    """Length-prefixed stdio packet writer for the hot local IPC path."""

    def __init__(self, stream, *, owns_stream: bool = False) -> None:
        self._stream = stream
        self._owns_stream = owns_stream
        self._lock = threading.Lock()

    def write(self, payload: Dict[str, Any], data: bytes = b"") -> None:
        meta = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        if len(meta) > STDIO_PACKET_MAX_META:
            raise ProtocolError("packet metadata is too large")
        if len(data) > STDIO_PACKET_MAX_DATA:
            raise ProtocolError("packet payload is too large")
        frame = STDIO_PACKET_HEADER.pack(STDIO_PACKET_MAGIC, len(meta), len(data)) + meta + data
        with self._lock:
            if self._stream is None:
                raise BrokenPipeError("writer stream is already closed")
            self._stream.write(frame)

    def close(self, *, abort: bool = False) -> None:
        with self._lock:
            stream = self._stream
            self._stream = None

        if stream is None or not self._owns_stream:
            return

        if abort and hasattr(stream, "detach"):
            try:
                detached = stream.detach()
            except Exception:
                detached = None
            if detached is not None:
                try:
                    detached.close()
                except Exception:
                    pass
            return

        try:
            stream.flush()
        except Exception:
            pass
        try:
            stream.close()
        except Exception:
            pass


class BinaryPacketReader:
    """Reads length-prefixed stdio packets from a binary stream."""

    def __init__(self, stream) -> None:
        self._stream = stream

    def _read_exact(self, size: int) -> Optional[bytes]:
        chunks = bytearray()
        while len(chunks) < size:
            chunk = self._stream.read(size - len(chunks))
            if chunk in (None, b""):
                if not chunks:
                    return None
                raise BrokenPipeError("unexpected EOF while reading packet")
            chunks.extend(chunk)
        return bytes(chunks)

    def read(self) -> Optional[tuple[Dict[str, Any], bytes]]:
        header = self._read_exact(STDIO_PACKET_HEADER.size)
        if header is None:
            return None
        magic, meta_len, data_len = STDIO_PACKET_HEADER.unpack(header)
        if magic != STDIO_PACKET_MAGIC:
            raise ProtocolError("invalid stdio packet magic")
        if meta_len > STDIO_PACKET_MAX_META:
            raise ProtocolError("stdio packet metadata exceeds limit")
        if data_len > STDIO_PACKET_MAX_DATA:
            raise ProtocolError("stdio packet payload exceeds limit")
        meta = self._read_exact(meta_len)
        if meta is None:
            raise BrokenPipeError("unexpected EOF while reading packet metadata")
        data = self._read_exact(data_len) if data_len else b""
        if data is None:
            raise BrokenPipeError("unexpected EOF while reading packet payload")
        try:
            payload = json.loads(meta.decode("utf-8"))
        except json.JSONDecodeError as exc:
            raise ProtocolError(str(exc)) from exc
        if not isinstance(payload, dict):
            raise ProtocolError("top-level JSON value must be an object")
        return payload, data


class SequenceCounter:
    """Monotonic sequence counter for bridge-originated events."""

    def __init__(self, initial: int = 0) -> None:
        self._value = initial
        self._lock = threading.Lock()

    def next(self) -> int:
        with self._lock:
            self._value += 1
            return self._value


def log_stderr(message: str) -> None:
    timestamp = time.strftime("%H:%M:%S")
    sys.stderr.write(f"[{timestamp}] {message}\n")
    sys.stderr.flush()



def load_python_can():
    try:
        import can  # type: ignore
    except ImportError as exc:  # pragma: no cover - depends on host env
        req_hint = Path(__file__).resolve().with_name("requirements-pycan.txt")
        raise DependencyError(
            f"python-can is not installed. Install with: pip install -r {req_hint}"
        ) from exc
    return can



def maybe_enumerate_gs_usb() -> Iterable[Dict[str, Any]]:
    """Best-effort gs_usb enumeration.

    Returns an iterable of dictionaries; empty when enumeration is unavailable or
    finds no matching devices.
    """

    try:
        from gs_usb.gs_usb import GsUsb  # type: ignore
    except Exception:
        return []

    devices = []
    try:
        for index, dev in enumerate(GsUsb.scan()):  # type: ignore[attr-defined]
            devices.append(
                {
                    "index": index,
                    "product": getattr(dev, "product", None),
                    "manufacturer": getattr(dev, "manufacturer", None),
                    "bus": getattr(dev, "bus", None),
                    "address": getattr(dev, "address", None),
                }
            )
    except Exception:
        return []
    return devices



def best_effort_usb_hint() -> Iterable[Dict[str, Any]]:
    try:
        import usb.core  # type: ignore
    except Exception:
        return []

    backend = None
    try:
        import libusb_package  # type: ignore

        backend = libusb_package.get_libusb1_backend()
    except Exception:
        backend = None

    try:
        kwargs = {
            "find_all": True,
            "idVendor": GS_USB_VENDOR_ID,
            "idProduct": GS_USB_PRODUCT_ID,
        }
        if backend is not None:
            kwargs["backend"] = backend
        found = usb.core.find(**kwargs)
    except Exception:
        return []

    if found is None:
        return []

    devices = []
    for index, dev in enumerate(found):
        devices.append(
            {
                "index": index,
                "product": getattr(dev, "product", None),
                "manufacturer": getattr(dev, "manufacturer", None),
                "bus": getattr(dev, "bus", None),
                "address": getattr(dev, "address", None),
            }
        )
    return devices



def normalize_interface_name(name: str) -> str:
    lowered = name.strip().lower()
    if lowered not in SUPPORTED_INTERFACES:
        raise BridgeConfigError(
            f"unsupported interface '{name}', expected one of: {', '.join(sorted(SUPPORTED_INTERFACES))}"
        )
    return lowered



def coerce_gs_usb_channel(channel_name: str) -> Any:
    text = channel_name.strip()
    if text == "":
        raise BridgeConfigError("gs_usb channel must not be empty")
    if text.isdecimal():
        return int(text, 10)
    return text



def validate_can_mode(interface_name: str, use_canfd: bool, use_brs: bool) -> None:
    if use_brs and not use_canfd:
        raise BridgeConfigError("BRS requires CAN FD")

    # Task 3 is intentionally conservative. The current Windows target hardware
    # and documented python-can parameters for gs_usb/slcan are classic CAN
    # oriented. Reject CAN FD requests explicitly instead of silently ignoring
    # them.
    if use_canfd:
        raise BridgeConfigError(
            f"interface '{interface_name}' is currently wired for classic CAN only in Task 3"
        )



def _maybe_attach_gs_usb_backend(kwargs: Dict[str, Any]) -> None:
    """Attach the same libusb backend path used by the standalone smoke tool.

    Task 3 already depends on ``libusb-package`` on Windows so that PyUSB can
    discover a bundled libusb-1.0 backend without asking the user to install a
    system-wide DLL manually. Task 4 must reuse that exact path; otherwise the
    bridge/runtime path falls back to PyUSB's default backend discovery and can
    fail with ``usb.core.NoBackendError: No backend available`` even though the
    standalone smoke probe succeeded earlier.
    """

    try:
        import usb.backend.libusb1  # type: ignore
    except Exception:
        return

    backend = None
    try:
        import libusb_package  # type: ignore

        get_backend = getattr(libusb_package, "get_libusb1_backend", None)
        if callable(get_backend):
            backend = get_backend()

        if backend is None:
            find_library = getattr(libusb_package, "find_library", None)
            if find_library is not None:
                backend = usb.backend.libusb1.get_backend(find_library=find_library)
    except Exception:
        backend = None

    if backend is not None:
        kwargs["backend"] = backend



def open_can_bus(options: BusOpenOptions):
    can = load_python_can()
    interface_name = normalize_interface_name(options.interface_name)
    validate_can_mode(interface_name, options.use_canfd, options.use_brs)

    kwargs: Dict[str, Any] = {
        "interface": interface_name,
        "bitrate": int(options.bitrate),
    }

    if interface_name == "gs_usb":
        kwargs["channel"] = coerce_gs_usb_channel(options.channel_name)
        _maybe_attach_gs_usb_backend(kwargs)
    elif interface_name == "slcan":
        channel = options.channel_name.strip()
        if channel == "":
            raise BridgeConfigError("slcan channel must not be empty (example: COM4@9600)")
        kwargs["channel"] = channel
    else:  # pragma: no cover - normalize_interface_name keeps this unreachable
        raise BridgeConfigError(f"unsupported interface '{interface_name}'")

    if hasattr(can, "ThreadSafeBus"):
        return can.ThreadSafeBus(**kwargs)
    return can.Bus(**kwargs)



def parse_hex_payload(text: str) -> bytes:
    compact = text.strip().lower()
    if compact.startswith("0x"):
        compact = compact[2:]
    if compact == "":
        return b""
    if len(compact) % 2 != 0:
        raise ProtocolError("hex payload must contain an even number of digits")
    try:
        return binascii.unhexlify(compact)
    except (binascii.Error, ValueError) as exc:
        raise ProtocolError("invalid hex payload") from exc



def hex_from_bytes(data: bytes | bytearray) -> str:
    return bytes(data).hex()



def monotonic_us() -> int:
    return time.monotonic_ns() // 1000



def build_can_message(can_module, *, can_id: int, extended: bool, fd: bool, brs: bool, rtr: bool, data: bytes):
    if rtr and fd:
        raise BridgeConfigError("RTR is invalid for CAN FD frames")
    return can_module.Message(
        arbitration_id=can_id,
        is_extended_id=extended,
        is_fd=fd,
        bitrate_switch=brs,
        is_remote_frame=rtr,
        data=bytearray(data),
        check=True,
    )



def message_to_protocol_dict(msg, *, seq: int) -> Dict[str, Any]:
    return {
        "type": "rx",
        "seq": seq,
        "can_id": int(msg.arbitration_id),
        "fd": bool(getattr(msg, "is_fd", False)),
    }



def require_int(obj: Dict[str, Any], key: str, *, minimum: Optional[int] = None) -> int:
    value = obj.get(key)
    if not isinstance(value, int):
        raise ProtocolError(f"field '{key}' must be an integer")
    if minimum is not None and value < minimum:
        raise ProtocolError(f"field '{key}' must be >= {minimum}")
    return value



def require_bool(obj: Dict[str, Any], key: str, *, default: Optional[bool] = None) -> bool:
    if key not in obj:
        if default is None:
            raise ProtocolError(f"field '{key}' must be a boolean")
        return default
    value = obj.get(key)
    if not isinstance(value, bool):
        raise ProtocolError(f"field '{key}' must be a boolean")
    return value



def require_str(obj: Dict[str, Any], key: str, *, allow_empty: bool = False, default: Optional[str] = None) -> str:
    if key not in obj:
        if default is None:
            raise ProtocolError(f"field '{key}' must be a string")
        return default
    value = obj.get(key)
    if not isinstance(value, str):
        raise ProtocolError(f"field '{key}' must be a string")
    if not allow_empty and value == "":
        raise ProtocolError(f"field '{key}' must not be empty")
    return value



def resolve_script_path(raw_path: str) -> Path:
    path = Path(raw_path)
    if path.is_absolute():
        return path
    return (Path(__file__).resolve().parent / raw_path).resolve()
