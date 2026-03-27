#!/usr/bin/env python3
"""python-can sidecar bridge for Windows raw CAN I/O.

Task 3 scope:
- keep ISO-TP/UDS in C
- expose only raw CAN/CAN FD control and frame events over JSON Lines
- work as a standalone process for manual smoke and later Task 4 integration
"""
from __future__ import annotations

import argparse
import json
import socket
import sys
import threading
from dataclasses import dataclass
from typing import Any, Dict, Optional, TextIO, Tuple

if __package__ in (None, ""):
    from pathlib import Path

    sys.path.insert(0, str(Path(__file__).resolve().parent))

from pycan_runtime import (
    PROTOCOL_VERSION,
    BridgeConfigError,
    BusOpenOptions,
    DependencyError,
    JsonLineWriter,
    ProtocolError,
    SequenceCounter,
    build_can_message,
    load_python_can,
    log_stderr,
    message_to_protocol_dict,
    open_can_bus,
    parse_hex_payload,
    require_bool,
    require_int,
    require_str,
)


@dataclass
class OpenSession:
    interface_name: str
    channel_name: str
    bitrate: int
    use_canfd: bool
    use_brs: bool
    bus: Any
    bus_lock: threading.Lock
    rx_thread: Optional[threading.Thread]
    stop_event: threading.Event


class BridgeServer:
    def __init__(self, ipc_mode: str, writer: JsonLineWriter) -> None:
        self.ipc_mode = ipc_mode
        self.writer = writer
        self.seq = SequenceCounter()
        self._state = "INIT"
        self._session: Optional[OpenSession] = None
        self._protocol_lock = threading.Lock()
        self._running = True
        self._peer_io_broken = False

    def emit(self, payload: Dict[str, Any]) -> bool:
        if self._peer_io_broken:
            return False
        try:
            self.writer.write(payload)
            return True
        except (BrokenPipeError, ConnectionResetError, OSError) as exc:
            self._peer_io_broken = True
            self._running = False
            log_stderr(f"peer disconnected while sending {payload.get('type', 'message')}: {exc}")
            return False

    def emit_error(self, *, code: str, detail: str, scope: str, reply_to: Optional[int] = None) -> None:
        payload: Dict[str, Any] = {
            "type": "error",
            "seq": self.seq.next(),
            "scope": scope,
            "code": code,
            "detail": detail,
        }
        if reply_to is not None:
            payload["reply_to"] = reply_to
        self.emit(payload)

    def close_session(self, *, reason: str, reply_to: Optional[int] = None, notify_peer: bool = True) -> None:
        session = self._session
        self._session = None
        if session is not None:
            session.stop_event.set()
            if session.rx_thread is not None:
                try:
                    session.rx_thread.join(timeout=1.0)
                except RuntimeError:
                    pass
            try:
                with session.bus_lock:
                    session.bus.shutdown()
            except Exception as exc:  # pragma: no cover - depends on backend
                log_stderr(f"bus shutdown warning: {exc}")
        self._state = "CLOSED"
        if notify_peer and not self._peer_io_broken:
            payload: Dict[str, Any] = {
                "type": "closed",
                "seq": self.seq.next(),
                "reason": reason,
            }
            if reply_to is not None:
                payload["reply_to"] = reply_to
            self.emit(payload)

    def _rx_loop(self, session: OpenSession) -> None:
        while not session.stop_event.is_set():
            try:
                with session.bus_lock:
                    msg = session.bus.recv(timeout=0.05)
            except Exception as exc:
                if not session.stop_event.is_set():
                    self._abort_session_from_rx_error(session, str(exc))
                break
            if msg is None:
                continue
            event = message_to_protocol_dict(msg, seq=self.seq.next())
            if not self.emit(event):
                break

    def _abort_session_from_rx_error(self, session: OpenSession, detail: str) -> None:
        session.stop_event.set()
        try:
            with session.bus_lock:
                session.bus.shutdown()
        except Exception as exc:  # pragma: no cover - depends on backend
            log_stderr(f"bus shutdown warning after RX failure: {exc}")
        with self._protocol_lock:
            if self._session is session:
                self._session = None
            self._state = "CLOSED"
            if not self._peer_io_broken:
                self.emit_error(code="BUS_RECV_FAILED", detail=detail, scope="recv")
                self.emit({"type": "closed", "seq": self.seq.next(), "reason": "rx_failure"})

    def _open_bus(self, message: Dict[str, Any], reply_to: int) -> None:
        if self._state not in {"HELLO", "CLOSED"}:
            self.emit_error(
                code="INVALID_STATE",
                detail=f"open is invalid in state {self._state}",
                scope="open",
                reply_to=reply_to,
            )
            return

        interface_name = require_str(message, "interface")
        channel_name = require_str(message, "channel")
        bitrate = require_int(message, "bitrate", minimum=1)
        use_canfd = require_bool(message, "fd", default=False)
        use_brs = require_bool(message, "brs", default=False)

        try:
            bus = open_can_bus(
                BusOpenOptions(
                    interface_name=interface_name,
                    channel_name=channel_name,
                    bitrate=bitrate,
                    use_canfd=use_canfd,
                    use_brs=use_brs,
                )
            )
        except (BridgeConfigError, DependencyError, Exception) as exc:
            self.emit_error(
                code="BUS_OPEN_FAILED",
                detail=str(exc),
                scope="open",
                reply_to=reply_to,
            )
            self._state = "CLOSED"
            return

        stop_event = threading.Event()
        session = OpenSession(
            interface_name=interface_name,
            channel_name=channel_name,
            bitrate=bitrate,
            use_canfd=use_canfd,
            use_brs=use_brs,
            bus=bus,
            bus_lock=threading.Lock(),
            rx_thread=None,
            stop_event=stop_event,
        )
        session.rx_thread = threading.Thread(
            target=self._rx_loop,
            args=(session,),
            name="pycan-bridge-rx",
            daemon=True,
        )
        self._session = session
        self._state = "OPEN"
        session.rx_thread.start()
        self.emit(
            {
                "type": "opened",
                "seq": self.seq.next(),
                "reply_to": reply_to,
                "version": PROTOCOL_VERSION,
                "interface": interface_name,
                "channel": channel_name,
                "bitrate": bitrate,
                "fd": use_canfd,
                "brs": use_brs,
            }
        )

    def _handle_tx(self, message: Dict[str, Any], reply_to: int) -> None:
        session = self._session
        if self._state != "OPEN" or session is None:
            self.emit_error(
                code="INVALID_STATE",
                detail="tx is only valid after opened",
                scope="tx",
                reply_to=reply_to,
            )
            return

        can_id = require_int(message, "can_id", minimum=0)
        extended = require_bool(message, "extended", default=False)
        fd = require_bool(message, "fd", default=False)
        brs = require_bool(message, "brs", default=False)
        rtr = require_bool(message, "rtr", default=False)
        payload = parse_hex_payload(require_str(message, "data", allow_empty=True, default=""))

        if fd or brs:
            self.emit_error(
                code="INVALID_MESSAGE",
                detail="Task 3 bridge only accepts classic CAN tx frames for gs_usb/slcan",
                scope="tx",
                reply_to=reply_to,
            )
            return

        try:
            can = load_python_can()
            frame = build_can_message(
                can,
                can_id=can_id,
                extended=extended,
                fd=fd,
                brs=brs,
                rtr=rtr,
                data=payload,
            )
            with session.bus_lock:
                session.bus.send(frame)
        except (DependencyError, BridgeConfigError, Exception) as exc:
            self.emit_error(
                code="BUS_SEND_FAILED",
                detail=str(exc),
                scope="tx",
                reply_to=reply_to,
            )
            return

        self.emit(
            {
                "type": "tx_done",
                "seq": self.seq.next(),
                "reply_to": reply_to,
                "status": "ok",
            }
        )

    def handle_message(self, message: Dict[str, Any]) -> bool:
        if not isinstance(message, dict):
            raise ProtocolError("top-level JSON value must be an object")

        msg_type = require_str(message, "type")
        msg_seq = require_int(message, "seq", minimum=0)

        with self._protocol_lock:
            if msg_type == "hello":
                if self._state not in {"INIT", "HELLO", "CLOSED"}:
                    self.emit_error(
                        code="INVALID_STATE",
                        detail=f"hello is invalid in state {self._state}",
                        scope="hello",
                        reply_to=msg_seq,
                    )
                    return True
                self._state = "HELLO"
                self.emit(
                    {
                        "type": "hello",
                        "seq": self.seq.next(),
                        "reply_to": msg_seq,
                        "version": PROTOCOL_VERSION,
                        "server": "pycan_bridge.py",
                    }
                )
                return True

            if msg_type == "ping":
                if self._state == "INIT":
                    self.emit_error(
                        code="INVALID_STATE",
                        detail="ping is only valid after hello",
                        scope="ping",
                        reply_to=msg_seq,
                    )
                    return True
                self.emit(
                    {
                        "type": "pong",
                        "seq": self.seq.next(),
                        "reply_to": msg_seq,
                    }
                )
                return True

            if msg_type == "open":
                self._open_bus(message, msg_seq)
                return True

            if msg_type == "tx":
                self._handle_tx(message, msg_seq)
                return True

            if msg_type == "close":
                self.close_session(reason=require_str(message, "reason", allow_empty=True, default="client_shutdown"), reply_to=msg_seq, notify_peer=not self._peer_io_broken)
                self._running = False
                return False

            self.emit_error(
                code="UNKNOWN_TYPE",
                detail=f"unsupported message type '{msg_type}'",
                scope="protocol",
                reply_to=msg_seq,
            )
            return True

    @property
    def running(self) -> bool:
        return self._running


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="python-can bridge sidecar for the C UDS client")
    parser.add_argument("--ipc", choices=("stdio", "tcp"), default="stdio", help="protocol transport: stdio JSONL or debug TCP JSONL")
    parser.add_argument("--tcp-host", default="127.0.0.1", help="debug TCP listen host")
    parser.add_argument("--tcp-port", type=int, default=29536, help="debug TCP listen port")
    parser.add_argument("--log-level", default="INFO", help="reserved for future structured logging")
    return parser


def open_stdio_endpoint() -> Tuple[TextIO, JsonLineWriter]:
    return sys.stdin, JsonLineWriter(sys.stdout, owns_stream=False)


def open_tcp_endpoint(host: str, port: int) -> Tuple[TextIO, JsonLineWriter]:
    server = socket.create_server((host, port), reuse_port=False)
    log_stderr(f"listening on tcp://{host}:{port}")
    conn, addr = server.accept()
    server.close()
    log_stderr(f"accepted TCP client from {addr[0]}:{addr[1]}")
    reader = conn.makefile("r", encoding="utf-8", newline="\n")
    writer = JsonLineWriter(conn, owns_stream=True)
    return reader, writer


def serve(reader: TextIO, writer: JsonLineWriter, ipc_mode: str) -> int:
    server = BridgeServer(ipc_mode=ipc_mode, writer=writer)

    try:
        while server.running:
            try:
                raw_line = reader.readline()
            except (BrokenPipeError, ConnectionResetError, OSError) as exc:
                if server.running and server._session is not None:  # intentional private access for peer-reset cleanup
                    log_stderr(f"peer disconnected while reading command stream: {exc}")
                    server.close_session(reason="peer_disconnect", notify_peer=False)
                break
            if raw_line == "":
                if server.running and server._session is not None:  # intentional private access for EOF cleanup
                    server.close_session(reason="peer_eof", notify_peer=False)
                break

            line = raw_line.strip()
            if line == "":
                continue

            try:
                payload = json.loads(line)
            except json.JSONDecodeError as exc:
                server.emit_error(code="INVALID_MESSAGE", detail=str(exc), scope="protocol")
                continue

            try:
                keep_running = server.handle_message(payload)
            except (ProtocolError, BridgeConfigError) as exc:
                reply_to = payload.get("seq") if isinstance(payload, dict) else None
                if not isinstance(reply_to, int):
                    reply_to = None
                server.emit_error(code="INVALID_MESSAGE", detail=str(exc), scope="protocol", reply_to=reply_to)
                keep_running = True
            except Exception as exc:  # pragma: no cover - defensive catch
                reply_to = payload.get("seq") if isinstance(payload, dict) else None
                if not isinstance(reply_to, int):
                    reply_to = None
                if not server._peer_io_broken:
                    server.emit_error(code="INTERNAL_EXCEPTION", detail=str(exc), scope="runtime", reply_to=reply_to)
                keep_running = not server._peer_io_broken

            if not keep_running:
                break
    finally:
        if server._session is not None:
            server.close_session(reason="bridge_shutdown", notify_peer=False)
        if ipc_mode == "tcp":
            try:
                reader.close()
            except Exception:
                pass
        writer.close(abort=server._peer_io_broken)

    return 0


def main(argv: Optional[list[str]] = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)

    if args.ipc == "tcp":
        reader, writer = open_tcp_endpoint(args.tcp_host, args.tcp_port)
    else:
        reader, writer = open_stdio_endpoint()

    return serve(reader, writer, args.ipc)


if __name__ == "__main__":
    sys.exit(main())
