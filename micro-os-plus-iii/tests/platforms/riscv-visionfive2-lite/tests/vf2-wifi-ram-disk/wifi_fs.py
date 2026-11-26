#!/usr/bin/env python3
"""
wifi_fs.py - file transfer between a PC and a board's RAM disk over Wi-Fi
(the MangoPi MQ-Pro and the VisionFive 2 Lite speak the same protocol).

The board runs a small TCP server (port 5000) in front of its 512 MiB RAM
disk (FatFs drive "1:").  The board must have joined the network first, e.g.

    python3 host/fs.py sh wifi join <ssid> <passphrase>      (over USB)

Protocol (text lines ending in LF, raw bytes where noted):

    ls                 -> "<size> <name>" per file, then "OK"
    df                 -> "TOTAL <bytes> FREE <bytes>", then "OK"
    put <name> <size>  -> "READY", <size> raw bytes, then "OK <crc32>"
    get <name>         -> "DATA <size> <crc32>", <size> raw bytes, then "OK"
    rm <name>          -> "OK"
    format             -> "OK"
    any failure        -> "ERR <text>"

The CRC-32 the board returns for "put" is computed from the file as it reads
it back from the RAM disk, so a matching value proves what is stored.

Usage:
    ./wifi_fs.py -H 192.168.1.9 ls
    ./wifi_fs.py -H 192.168.1.9 df
    ./wifi_fs.py -H 192.168.1.9 put local.bin [remote.bin]
    ./wifi_fs.py -H 192.168.1.9 get remote.bin [local.bin]
    ./wifi_fs.py -H 192.168.1.9 rm remote.bin
    ./wifi_fs.py -H 192.168.1.9 format

Copyright (c) 2026 Dan. MIT license.
"""

import argparse
import os
import socket
import sys
import time
import zlib

DEFAULT_TCP_PORT = 5000


class BoardError(RuntimeError):
    pass


class Board:
    """One TCP connection to the board's RAM-disk file server."""

    def __init__(self, host, port=DEFAULT_TCP_PORT, timeout=30.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buffer = bytearray()

    def close(self):
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # -- raw I/O -----------------------------------------------------------
    def _fill(self):
        chunk = self.sock.recv(65536)
        if not chunk:
            raise BoardError("the board closed the connection")
        self.buffer += chunk

    def read_line(self):
        while True:
            index = self.buffer.find(b"\n")
            if index >= 0:
                line = bytes(self.buffer[:index])
                del self.buffer[: index + 1]
                return line.decode(errors="replace").rstrip("\r")
            self._fill()

    def read_exact(self, count, progress=None):
        chunks = []
        got = 0
        if self.buffer:
            head = bytes(self.buffer[:count])
            del self.buffer[: len(head)]
            chunks.append(head)
            got = len(head)
        while got < count:
            chunk = self.sock.recv(min(65536, count - got))
            if not chunk:
                raise BoardError(f"connection closed after {got}/{count} bytes")
            chunks.append(chunk)
            got += len(chunk)
            if progress:
                progress(got, count)
        return b"".join(chunks)

    def _result(self):
        """Collect reply lines up to OK (returned) or ERR (raised)."""
        lines = []
        while True:
            line = self.read_line()
            if line == "OK" or line.startswith("OK "):
                return lines, line[3:]
            if line.startswith("ERR"):
                raise BoardError(line[3:].strip() or "board error")
            lines.append(line)

    # -- commands ----------------------------------------------------------
    def ls(self):
        """Return [(name, size), ...]."""
        self.sock.sendall(b"ls\n")
        lines, _ = self._result()
        files = []
        for line in lines:
            size, name = line.split(" ", 1)
            files.append((name, int(size)))
        return files

    def df(self):
        """Return (total_bytes, free_bytes)."""
        self.sock.sendall(b"df\n")
        lines, _ = self._result()
        _, total, _, free = lines[0].split()
        return int(total), int(free)

    def rm(self, name):
        self.sock.sendall(f"rm {name}\n".encode())
        self._result()

    def format(self):
        self.sock.sendall(b"format\n")
        self._result()

    def put(self, name, data, progress=None):
        """Store `data` as `name`; returns the CRC-32 the board read back."""
        self.sock.sendall(f"put {name} {len(data)}\n".encode())
        reply = self.read_line()
        if reply.startswith("ERR"):
            raise BoardError(reply[3:].strip())
        if reply != "READY":
            raise BoardError(f"unexpected reply: {reply!r}")
        view = memoryview(data)
        sent = 0
        while sent < len(view):
            sent += self.sock.send(view[sent : sent + 16384])
            if progress:
                progress(sent, len(view))
        _, tail = self._result()
        board_crc = int(tail, 16)
        if board_crc != zlib.crc32(data):
            raise BoardError(
                f"CRC mismatch for {name}: board {board_crc:08x}, "
                f"local {zlib.crc32(data):08x}")
        return board_crc

    def verdict(self, passed, text=""):
        """VisionFive 2 test: report the PC's verdict; the board then ends
        its run with the matching RESULT line."""
        line = f"verdict {'PASS' if passed else 'FAIL'} {text}".rstrip()
        self.sock.sendall(line.replace("\n", " ").encode() + b"\n")
        self._result()

    def get(self, name, progress=None):
        """Return the contents of `name` (CRC-32 checked)."""
        self.sock.sendall(f"get {name}\n".encode())
        header = self.read_line()
        if header.startswith("ERR"):
            raise BoardError(header[3:].strip())
        fields = header.split()
        if len(fields) != 3 or fields[0] != "DATA":
            raise BoardError(f"unexpected reply: {header!r}")
        size, board_crc = int(fields[1]), int(fields[2], 16)
        data = self.read_exact(size, progress)
        self._result()
        if zlib.crc32(data) != board_crc:
            raise BoardError(
                f"CRC mismatch for {name}: board {board_crc:08x}, "
                f"received {zlib.crc32(data):08x}")
        return data


def progress_printer(label):
    state = {"last": 0.0}

    def show(done, total):
        now = time.monotonic()
        if done == total or now - state["last"] >= 0.2:
            state["last"] = now
            percent = 100.0 * done / total if total else 100.0
            print(f"\r  {label} {done}/{total} ({percent:5.1f} %)", end="", flush=True)
            if done == total:
                print()

    return show


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("-H", "--host", required=True, help="board IP address")
    parser.add_argument("-P", "--tcp-port", type=int, default=DEFAULT_TCP_PORT)
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("ls")
    sub.add_parser("df")
    sub.add_parser("format")
    put = sub.add_parser("put")
    put.add_argument("local")
    put.add_argument("remote", nargs="?")
    get = sub.add_parser("get")
    get.add_argument("remote")
    get.add_argument("local", nargs="?")
    rm = sub.add_parser("rm")
    rm.add_argument("remote")
    args = parser.parse_args()

    with Board(args.host, args.tcp_port) as board:
        if args.action == "ls":
            for name, size in board.ls():
                print(f"{size:>12} {name}")
        elif args.action == "df":
            total, free = board.df()
            print(f"total {total / 1048576:.1f} MiB, used {(total - free) / 1048576:.2f} MiB, "
                  f"free {free / 1048576:.1f} MiB")
        elif args.action == "format":
            board.format()
            print("RAM disk formatted")
        elif args.action == "rm":
            board.rm(args.remote)
            print(f"removed {args.remote}")
        elif args.action == "put":
            remote = args.remote or os.path.basename(args.local)
            with open(args.local, "rb") as handle:
                data = handle.read()
            start = time.monotonic()
            crc = board.put(remote, data, progress_printer("sent"))
            elapsed = max(time.monotonic() - start, 1e-6)
            print(f"stored {remote}: {len(data)} bytes, crc32 {crc:08x}, "
                  f"{len(data) / 1024 / elapsed:.0f} KiB/s")
        elif args.action == "get":
            local = args.local or os.path.basename(args.remote)
            start = time.monotonic()
            data = board.get(args.remote, progress_printer("received"))
            elapsed = max(time.monotonic() - start, 1e-6)
            with open(local, "wb") as handle:
                handle.write(data)
            print(f"saved {local}: {len(data)} bytes, {len(data) / 1024 / elapsed:.0f} KiB/s")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (BoardError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
