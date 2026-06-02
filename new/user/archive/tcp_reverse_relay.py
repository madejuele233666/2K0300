#!/usr/bin/env python3
"""User-space TCP reverse relay for Windows-hosted board callbacks.

The Windows side accepts the board connection and a WSL-initiated tunnel.
The WSL side connects that tunnel to an archived local listener such as
./debug.sh steering legacy.
"""

from __future__ import annotations

import argparse
import selectors
import socket
import sys
import time
from typing import Optional, Tuple


SocketPair = Tuple[socket.socket, socket.socket]


def log(message: str) -> None:
    print(message, flush=True)


def parse_host_port(text: str) -> Tuple[str, int]:
    host, separator, port_text = text.rpartition(":")
    if not separator or not host:
        raise argparse.ArgumentTypeError(f"expected HOST:PORT, got {text!r}")
    try:
        port = int(port_text)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"invalid port in {text!r}") from error
    if port <= 0 or port > 65535:
        raise argparse.ArgumentTypeError(f"port out of range in {text!r}")
    return host, port


def set_socket_options(sock: socket.socket) -> None:
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)


def listen(endpoint: Tuple[str, int], backlog: int = 1) -> socket.socket:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(endpoint)
    server.listen(backlog)
    return server


def connect_with_retry(endpoint: Tuple[str, int], retry_s: float, label: str) -> socket.socket:
    while True:
        try:
            sock = socket.create_connection(endpoint, timeout=3.0)
            set_socket_options(sock)
            log(f"[{label}] connected {endpoint[0]}:{endpoint[1]}")
            return sock
        except OSError as error:
            log(f"[{label}] connect failed: {error}; retrying in {retry_s:.1f}s")
            time.sleep(retry_s)


def accept_one(server: socket.socket, label: str) -> socket.socket:
    sock, address = server.accept()
    set_socket_options(sock)
    log(f"[{label}] accepted {address[0]}:{address[1]}")
    return sock


def bridge(left: socket.socket, right: socket.socket, label: str) -> None:
    selector = selectors.DefaultSelector()
    sockets = {left: right, right: left}
    for sock in sockets:
        sock.setblocking(False)
        selector.register(sock, selectors.EVENT_READ)

    total_left_to_right = 0
    total_right_to_left = 0
    try:
        while True:
            events = selector.select(timeout=1.0)
            if not events:
                continue
            for key, _ in events:
                src = key.fileobj
                dst = sockets[src]
                try:
                    chunk = src.recv(65536)
                except OSError as error:
                    log(f"[{label}] receive failed: {error}")
                    return
                if not chunk:
                    log(
                        f"[{label}] closed bytes_lr={total_left_to_right} "
                        f"bytes_rl={total_right_to_left}"
                    )
                    return
                try:
                    dst.sendall(chunk)
                except OSError as error:
                    log(f"[{label}] send failed: {error}")
                    return
                if src is left:
                    total_left_to_right += len(chunk)
                else:
                    total_right_to_left += len(chunk)
    finally:
        selector.close()
        for sock in sockets:
            try:
                sock.close()
            except OSError:
                pass


def run_windows(args: argparse.Namespace) -> int:
    board_server = listen(args.board_listen)
    tunnel_server = listen(args.tunnel_listen)
    log(
        "[windows] board_listen="
        f"{args.board_listen[0]}:{args.board_listen[1]} "
        "tunnel_listen="
        f"{args.tunnel_listen[0]}:{args.tunnel_listen[1]}"
    )
    try:
        while True:
            tunnel: Optional[socket.socket] = None
            board: Optional[socket.socket] = None
            try:
                tunnel = accept_one(tunnel_server, "windows.tunnel")
                board = accept_one(board_server, "windows.board")
                bridge(board, tunnel, "windows")
            except KeyboardInterrupt:
                return 130
            except OSError as error:
                log(f"[windows] relay failed: {error}")
            finally:
                for sock in (board, tunnel):
                    if sock is not None:
                        try:
                            sock.close()
                        except OSError:
                            pass
            if not args.reconnect:
                return 0
    finally:
        board_server.close()
        tunnel_server.close()


def run_wsl(args: argparse.Namespace) -> int:
    log(
        "[wsl] tunnel="
        f"{args.tunnel[0]}:{args.tunnel[1]} "
        "target="
        f"{args.target[0]}:{args.target[1]}"
    )
    while True:
        tunnel: Optional[socket.socket] = None
        target: Optional[socket.socket] = None
        try:
            tunnel = connect_with_retry(args.tunnel, args.retry_s, "wsl.tunnel")
            target = connect_with_retry(args.target, args.retry_s, "wsl.target")
            bridge(tunnel, target, "wsl")
        except KeyboardInterrupt:
            return 130
        finally:
            for sock in (target, tunnel):
                if sock is not None:
                    try:
                        sock.close()
                    except OSError:
                        pass
        if not args.reconnect:
            return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="mode", required=True)

    windows = subparsers.add_parser("windows", help="listen on Windows for board and WSL tunnel")
    windows.add_argument("--board-listen", type=parse_host_port, required=True)
    windows.add_argument("--tunnel-listen", type=parse_host_port, required=True)
    windows.add_argument("--reconnect", action="store_true")

    wsl = subparsers.add_parser("wsl", help="connect from WSL to Windows tunnel and local target")
    wsl.add_argument("--tunnel", type=parse_host_port, required=True)
    wsl.add_argument("--target", type=parse_host_port, required=True)
    wsl.add_argument("--retry-s", type=float, default=0.2)
    wsl.add_argument("--reconnect", action="store_true")
    return parser


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.mode == "windows":
        return run_windows(args)
    if args.mode == "wsl":
        return run_wsl(args)
    parser.error(f"unsupported mode: {args.mode}")
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
