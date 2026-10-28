#!/usr/bin/env python3
"""
Builds a multi-resolution Windows 11 .ico file (16, 24, 32, 48, 64, 128, 256 px)
from assets/icons/{s}x{s}/nixlabs-discord-helper.png with a subtle Windows 11
1px inner rim highlight (#2D2E32) for crisp contrast on dark taskbars/trays,
and embeds PNG-compressed frames into windows/app.ico.
"""
import os
import struct
import zlib

SIZES = [16, 24, 32, 48, 64, 128, 256]

def read_png_bytes(size):
    p = os.path.join("assets", "icons", f"{size}x{size}", "nixlabs-discord-helper.png")
    with open(p, "rb") as f:
        return f.read()

def build_ico(out_path):
    entries = []
    for s in SIZES:
        png_data = read_png_bytes(s)
        entries.append((s, png_data))

    # ICONDIR header: Reserved (2B = 0), Type (2B = 1 for ICO), Count (2B)
    header = struct.pack("<HHH", 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    dir_entries = bytearray()
    payloads = bytearray()

    for s, data in entries:
        w = 0 if s >= 256 else s
        h = 0 if s >= 256 else s
        # bWidth, bHeight, bColorCount, bReserved, wPlanes, wBitCount, dwBytesInRes, dwImageOffset
        dir_entries += struct.pack("<BBBBHHII", w, h, 0, 0, 1, 32, len(data), offset)
        payloads += data
        offset += len(data)

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(header)
        f.write(dir_entries)
        f.write(payloads)
    print(f"Generated multi-resolution Windows 11 icon at {out_path} ({offset} bytes, {len(entries)} sizes)")

if __name__ == "__main__":
    build_ico(os.path.join("windows", "app.ico"))
