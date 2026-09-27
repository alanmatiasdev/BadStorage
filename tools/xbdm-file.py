#!/usr/bin/env python3
"""One-time XBDM file transfer for installing a local Xbox boot title.

This is only a deployment helper. The Xbox does not connect to this script at
boot or after installation. Uploads refuse to replace an existing remote file.
"""

import argparse
import hashlib
from pathlib import Path
import re
import socket
import struct


class XbdmError(RuntimeError):
    pass


class Xbdm:
    def __init__(self, host: str):
        self.socket = socket.create_connection((host, 730), timeout=10)
        self.socket.settimeout(15)
        self.stream = self.socket.makefile("rb")
        self.expect(self.line(), "201")

    def __enter__(self):
        return self

    def __exit__(self, *_):
        try:
            self.socket.sendall(b"bye\r\n")
        except OSError:
            pass
        self.stream.close()
        self.socket.close()

    def line(self) -> str:
        line = self.stream.readline()
        if not line:
            raise XbdmError("XBDM closed the connection")
        return line.decode("ascii", "replace").rstrip("\r\n")

    @staticmethod
    def expect(response: str, code: str):
        if not response.startswith(code):
            raise XbdmError(f"XBDM: {response}")

    def command(self, command: str) -> str:
        if "\r" in command or "\n" in command:
            raise ValueError("newline in XBDM command")
        self.socket.sendall(command.encode("ascii") + b"\r\n")
        return self.line()

    def receive_exact(self, count: int) -> bytes:
        data = self.stream.read(count)
        if len(data) != count:
            raise XbdmError(f"short XBDM binary response ({len(data)}/{count})")
        return data

    def directory(self, path: str) -> set[str]:
        self.expect(self.command(f'dirlist name="{path}"'), "202")
        names = set()
        while (line := self.line()) != ".":
            match = re.search(r'name="([^"]+)"', line)
            if match:
                names.add(match.group(1).casefold())
        return names

    def get_file(self, path: str) -> bytes:
        self.expect(self.command(f'getfile name="{path}"'), "203")
        length = struct.unpack("<I", self.receive_exact(4))[0]
        return self.receive_exact(length)

    def put_new_file(self, path: str, data: bytes):
        parent, separator, name = path.rpartition("\\")
        if not separator or not name:
            raise ValueError("remote path must include a directory and filename")
        if name.casefold() in self.directory(parent + "\\"):
            raise XbdmError(f"remote file already exists: {path}")
        self.expect(self.command(f'sendfile name="{path}" length=0x{len(data):X}'), "204")
        self.socket.sendall(data)


def remote_path(value: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9]+:\\[^\"\r\n]+", value):
        raise argparse.ArgumentTypeError("expected a quoted Xbox path such as USB0:\\Apps\\file.xex")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    commands = parser.add_subparsers(dest="command", required=True)
    get_command = commands.add_parser("get", help="download without overwriting a local file")
    get_command.add_argument("remote", type=remote_path)
    get_command.add_argument("local", type=Path)
    put_command = commands.add_parser("put-new", help="upload without overwriting a remote file")
    put_command.add_argument("local", type=Path)
    put_command.add_argument("remote", type=remote_path)
    args = parser.parse_args()

    if args.command == "get":
        if args.local.exists():
            raise XbdmError(f"local file already exists: {args.local}")
        with Xbdm(args.host) as client:
            data = client.get_file(args.remote)
        with args.local.open("xb") as output:
            output.write(data)
    else:
        data = args.local.read_bytes()
        with Xbdm(args.host) as client:
            client.put_new_file(args.remote, data)
        # The sender's final status varies by XBDM version. A separate readback
        # verifies completion and every byte, regardless of that status.
        with Xbdm(args.host) as client:
            if client.get_file(args.remote) != data:
                raise XbdmError("uploaded file differs from the source")
    print(f"SHA-256: {hashlib.sha256(data).hexdigest()}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, XbdmError, ValueError) as error:
        raise SystemExit(f"error: {error}")
