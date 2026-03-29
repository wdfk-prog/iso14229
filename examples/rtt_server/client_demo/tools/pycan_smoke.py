#!/usr/bin/env python3
"""Standalone smoke probe for python-can + canlight/candleLight workflows."""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Optional

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from pycan_runtime import (
    BridgeConfigError,
    BusOpenOptions,
    build_can_message,
    best_effort_usb_hint,
    hex_from_bytes,
    load_python_can,
    maybe_enumerate_gs_usb,
    open_can_bus,
    parse_hex_payload,
)


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Standalone python-can smoke test for canlight/candleLight adapters")
    parser.add_argument("--interface", default="gs_usb", choices=("gs_usb", "slcan"), help="python-can interface")
    parser.add_argument("--channel", default="0", help="gs_usb channel index or slcan port, e.g. COM4@9600")
    parser.add_argument("--bitrate", type=int, default=1000000, help="arbitration bitrate in bit/s")
    parser.add_argument("--recv-timeout-ms", type=int, default=500, help="receive wait time after send")
    parser.add_argument("--send-id", default="7E0", help="CAN identifier for optional transmit test")
    parser.add_argument("--send-data", default="1003", help="hex payload for optional transmit test")
    parser.add_argument("--extid", action="store_true", help="send as extended CAN frame")
    parser.add_argument("--no-send", action="store_true", help="open the bus without sending a test frame")
    parser.add_argument("--json", action="store_true", help="emit machine-readable result JSON")
    return parser


def emit_result(args, payload) -> None:
    if args.json:
        print(json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True))
        return

    for key, value in payload.items():
        print(f"{key}: {value}")


def safe_channel_info(bus) -> str:
    """Return channel info without triggering buggy __str__ paths."""
    try:
        value = getattr(bus, "channel_info", None)
        if value is not None:
            return str(value)
    except Exception:
        pass

    for attr in ("channel", "_channel", "index"):
        try:
            value = getattr(bus, attr, None)
            if value is not None:
                return f"{bus.__class__.__name__}({attr}={value})"
        except Exception:
            pass

    return bus.__class__.__name__


def main(argv: Optional[list[str]] = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)

    enumeration = list(maybe_enumerate_gs_usb())
    usb_hint = list(best_effort_usb_hint())

    result = {
        "interface": args.interface,
        "channel": args.channel,
        "bitrate": args.bitrate,
        "enumerated_devices": enumeration,
        "usb_hint_devices": usb_hint,
        "opened": False,
        "sent": False,
        "received": None,
    }

    try:
        can = load_python_can()
        with open_can_bus(
            BusOpenOptions(
                interface_name=args.interface,
                channel_name=args.channel,
                bitrate=args.bitrate,
            )
        ) as bus:
            result["opened"] = True
            result["channel_info"] = safe_channel_info(bus)
            if not args.no_send:
                frame = build_can_message(
                    can,
                    can_id=int(args.send_id, 16),
                    extended=args.extid,
                    fd=False,
                    brs=False,
                    rtr=False,
                    data=parse_hex_payload(args.send_data),
                )
                bus.send(frame)
                result["sent"] = True
                deadline = time.monotonic() + (args.recv_timeout_ms / 1000.0)
                while time.monotonic() < deadline:
                    msg = bus.recv(timeout=0.05)
                    if msg is None:
                        continue
                    result["received"] = {
                        "can_id": int(msg.arbitration_id),
                        "extended": bool(getattr(msg, "is_extended_id", False)),
                        "fd": bool(getattr(msg, "is_fd", False)),
                        "brs": bool(getattr(msg, "bitrate_switch", False)),
                        "data": hex_from_bytes(getattr(msg, "data", b"")),
                    }
                    break
    except (BridgeConfigError, Exception) as exc:
        result["error"] = str(exc)
        emit_result(args, result)
        return 1

    emit_result(args, result)
    return 0


if __name__ == "__main__":
    sys.exit(main())
