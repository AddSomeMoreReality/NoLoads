"""Converts the plugin's texture dumps (F11: logs/portal-<n>.dds, main-<n>.dds, graded-<n>.dds, ldr-<n>.dds;
DX10 header) to PNG for viewing. Standard library only.

Formats:
  10 R16G16B16A16_FLOAT (HDR: portal, main, graded) - tone mapped: c / (1 + c) per channel times --exposure,
     then gamma 2.2;
  24 R10G10B10A2_UNORM, 28 R8G8B8A8_UNORM, 29 R8G8B8A8_UNORM_SRGB (LDR: ldr) - written as stored (the game's
     output is already display encoded); --exposure scales it.
Alpha is ignored.

Usage: python tools/portal_dump_to_png.py logs/portal-1.dds [more.dds ...] [--exposure 1.0]
"""
import argparse
import struct
import zlib

R16G16B16A16_FLOAT = 10
R10G10B10A2_UNORM = 24
R8G8B8A8_UNORM = 28
R8G8B8A8_UNORM_SRGB = 29
FORMATS = {R16G16B16A16_FLOAT: 8, R10G10B10A2_UNORM: 4, R8G8B8A8_UNORM: 4, R8G8B8A8_UNORM_SRGB: 4}


def read_dds(path):
    data = open(path, "rb").read()
    if data[:4] != b"DDS ":
        raise SystemExit(f"{path}: not a DDS file")
    height, width = struct.unpack_from("<II", data, 12)
    fourcc = data[84:88]
    if fourcc != b"DX10":
        raise SystemExit(f"{path}: expected a DX10 header, got {fourcc!r}")
    fmt = struct.unpack_from("<I", data, 128)[0]
    if fmt not in FORMATS:
        raise SystemExit(f"{path}: DXGI format {fmt}, supported: {sorted(FORMATS)}")
    pixels = data[148:148 + width * height * FORMATS[fmt]]
    return width, height, fmt, pixels


def channels(fmt, pixels, count):
    """RGB of every pixel as floats: HDR values, or 0..1 for the UNORM formats."""
    if fmt == R16G16B16A16_FLOAT:
        halves = struct.unpack(f"<{count * 4}e", pixels)
        for i in range(count):
            yield halves[i * 4:i * 4 + 3]
    elif fmt == R10G10B10A2_UNORM:
        for (word,) in struct.iter_unpack("<I", pixels):
            yield ((word & 0x3FF) / 1023.0, ((word >> 10) & 0x3FF) / 1023.0, ((word >> 20) & 0x3FF) / 1023.0)
    else:
        for i in range(count):
            yield (pixels[i * 4] / 255.0, pixels[i * 4 + 1] / 255.0, pixels[i * 4 + 2] / 255.0)


def to_png(width, height, fmt, pixels, exposure):
    hdr = fmt == R16G16B16A16_FLOAT
    rows = []
    stats = [0.0, 0.0, 0]  # max, sum of the channel mean, non-finite count
    row = bytearray([0])  # filter: none
    for index, rgb in enumerate(channels(fmt, pixels, width * height)):
        out = []
        finite = True
        for c in rgb:
            if c != c or c in (float("inf"), float("-inf")):
                stats[2] += 1
                finite = False
                c = 0.0
            c = max(c, 0.0) * exposure
            stats[0] = max(stats[0], c)
            v = (c / (1.0 + c)) ** (1.0 / 2.2) if hdr else min(c, 1.0)
            out.append(int(round(255.0 * v)))
        stats[1] += sum(rgb) / 3.0 if finite else 0.0
        row += bytes(out)
        if (index + 1) % width == 0:
            rows.append(bytes(row))
            row = bytearray([0])

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
    png += chunk(b"IEND", b"")
    return png, stats


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dds", nargs="+")
    parser.add_argument("--exposure", type=float, default=1.0)
    options = parser.parse_args()
    for path in options.dds:
        width, height, fmt, pixels = read_dds(path)
        png, (peak, total, bad) = to_png(width, height, fmt, pixels, options.exposure)
        out = path[:-4] + ".png"
        with open(out, "wb") as f:
            f.write(png)
        print(f"{out}: {width}x{height}, format {fmt}, peak {peak:.3f}, mean {total / (width * height):.4f}, non-finite {bad}")


if __name__ == "__main__":
    main()
