#!/usr/bin/env python3
"""Makes an app icon from the original Red Alert 95 program icon.

Reads the icon resources of the game's Windows executable (INSTALL/RA95.EXE on
either disc), picks the largest, most colourful image (32x32, 256 colours) and
enlarges it with hard pixel edges to a 1024x1024 PNG.

The icon is EA's artwork, so the result is a local file and stays out of the
repository: write it to assets/AppIcon-1024.png (git-ignored) and the iOS build
uses it instead of the original-design icon in ios/Assets.xcassets.

    python3 ios/icon/make_exe_icon.py assets/redalert/allies/INSTALL/RA95.EXE assets/AppIcon-1024.png

Only the Python standard library is used. The executable is parsed, never run.
"""

import struct
import sys
import zlib

RT_ICON = 3
RT_GROUP_ICON = 14
OUTPUT_SIZE = 1024


def fail(message):
    sys.exit("make_exe_icon: " + message)


class Resources:
    def __init__(self, data):
        self.data = data
        if data[:2] != b"MZ":
            fail("not a Windows executable")
        pe = self.u32(0x3C)
        if data[pe:pe + 4] != b"PE\0\0":
            fail("missing PE header")
        sections = self.u16(pe + 6)
        optional_size = self.u16(pe + 20)
        optional = pe + 24
        directories = optional + (96 if self.u16(optional) == 0x10B else 112)
        resource_rva = self.u32(directories + 2 * 8)
        if resource_rva == 0:
            fail("the executable has no resources")
        self.sections = []
        table = optional + optional_size
        for index in range(sections):
            entry = table + index * 40
            virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, entry + 8)
            self.sections.append((virtual_address, max(virtual_size, raw_size), raw_pointer))
        self.base = self.offset(resource_rva)

    def u16(self, offset):
        return struct.unpack_from("<H", self.data, offset)[0]

    def u32(self, offset):
        return struct.unpack_from("<I", self.data, offset)[0]

    def offset(self, rva):
        for address, size, raw in self.sections:
            if address <= rva < address + size:
                return rva - address + raw
        fail("resource address outside every section")

    def entries(self, directory):
        named, numbered = struct.unpack_from("<HH", self.data, directory + 12)
        for index in range(named + numbered):
            yield struct.unpack_from("<II", self.data, directory + 16 + index * 8)

    def leaf(self, target):
        # Follow subdirectories (name, then language) to the first data entry.
        while target & 0x80000000:
            target = next(self.entries(self.base + (target & 0x7FFFFFFF)))[1]
        rva, size = struct.unpack_from("<II", self.data, self.base + target)
        start = self.offset(rva)
        return self.data[start:start + size]

    def of_type(self, kind):
        for name, target in self.entries(self.base):
            if name == kind:
                return {item: self.leaf(child) for item, child in self.entries(self.base + (target & 0x7FFFFFFF))}
        return {}


def largest_icon(resources):
    images = resources.of_type(RT_ICON)
    best = None
    for group in resources.of_type(RT_GROUP_ICON).values():
        count = struct.unpack_from("<H", group, 4)[0]
        for index in range(count):
            width, height, _, _, _, bits, _, icon_id = struct.unpack_from("<BBBBHHIH", group, 6 + index * 14)
            key = (width or 256, bits)
            if icon_id in images and (best is None or key > best[0]):
                best = (key, images[icon_id])
    if best is None:
        fail("no icon found in the executable")
    return best[1]


def decode(dib):
    """Returns (size, rows of (r, g, b)) for a 1/4/8/24/32-bit icon DIB."""
    header, width, double_height, _, bits, compression = struct.unpack_from("<IiiHHI", dib, 0)
    colours_used = struct.unpack_from("<I", dib, 32)[0]
    if compression != 0:
        fail("compressed (PNG) icons are not supported")
    height = double_height // 2
    palette_size = colours_used or (1 << bits if bits <= 8 else 0)
    palette = [struct.unpack_from("<BBBx", dib, header + index * 4) for index in range(palette_size)]
    pixels = header + palette_size * 4
    stride = ((width * bits + 31) // 32) * 4
    rows = []
    for y in range(height):
        row_start = pixels + (height - 1 - y) * stride  # bottom-up
        row = []
        for x in range(width):
            if bits <= 8:
                bit = x * bits
                value = (dib[row_start + bit // 8] >> (8 - bits - bit % 8)) & ((1 << bits) - 1)
                blue, green, red = palette[value]
            else:
                blue, green, red = dib[row_start + x * (bits // 8):row_start + x * (bits // 8) + 3]
            row.append((red, green, blue))
        rows.append(row)
    if width != height:
        fail("expected a square icon, got %dx%d" % (width, height))
    return width, rows


def write_png(path, size, rows):
    raw = b"".join(b"\0" + bytes(channel for pixel in row for channel in pixel) for row in rows)

    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(png)


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: make_exe_icon.py <RA95.EXE> [output.png]")
    source = sys.argv[1]
    output = sys.argv[2] if len(sys.argv) > 2 else "assets/AppIcon-1024.png"
    with open(source, "rb") as handle:
        data = handle.read()
    size, rows = decode(largest_icon(Resources(data)))
    if OUTPUT_SIZE % size:
        fail("icon size %d does not divide %d" % (size, OUTPUT_SIZE))
    scale = OUTPUT_SIZE // size
    # Nearest-neighbour enlargement keeps the original pixels crisp.
    enlarged = [[pixel for pixel in row for _ in range(scale)] for row in rows for _ in range(scale)]
    write_png(output, OUTPUT_SIZE, enlarged)
    print("wrote %s from the %dx%d icon" % (output, size, size))


if __name__ == "__main__":
    main()
