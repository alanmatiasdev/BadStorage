#!/usr/bin/env python3
"""Create a DashLaunch config with the local SSD bootstrap as Default."""

from pathlib import Path
import sys


source, destination = map(Path, sys.argv[1:])
data = source.read_bytes()
old = b"Default = Usb:\\Apps\\Aurora\\Aurora.xex"
new = b"Default = Usb:\\Apps\\Aurora\\BadStorageBoot-v5.xex"
if data.count(old) != 1:
    raise SystemExit(f"expected one Aurora Default entry, found {data.count(old)}")
destination.write_bytes(data.replace(old, new))
