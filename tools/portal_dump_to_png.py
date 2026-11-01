"""Converts the portal render target dumps (F11 at InteriorSim stage 6: logs/portal-<n>.dds, DX10 header,
R16G16B16A16_FLOAT, HDR) to PNG for viewing. Standard library only.

Tone mapping: c / (1 + c) per channel times --exposure, then gamma 2.2. Alpha is ignored.

Usage: python tools/portal_dump_to_png.py logs/portal-1.dds [more.dds ...] [--exposure 1.0]
"""
import argparse
import struct
import zlib

DXGI_FORMAT_R16G16B16A16_FLOAT = 10


def read_dds(path):
    data = open(path, "rb").read()
    if data[:4] != b"DDS ":
        raise SystemExit(f"{path}: not a DDS file")
    height, width = struct.unpack_from("<II", data, 12)
    fourcc = data[84:88]
    if fourcc != b"DX10":
        raise SystemExit(f"{path}: expected a DX10 header, got {fourcc!r}")
    fmt = struct.unpack_from("<I", data, 128)[0]
    if fmt != DXGI_FORMAT_R16G16B16A16_FLOAT:
        raise SystemExit(f"{path}: DXGI format {fmt}, only R16G16B16A16_FLOAT (10) is supported")
    pixels = data[148:148 + width * height * 8]
    return width, height, pixels


def to_png(width, height, pixels, exposure):
    halves = struct.unpack(f"<{width * height * 4}e", pixels)
    rows = []
    stats = [0.0, 0.0, 0]  # max, sum of luminance, non-finite count
    for y in range(height):
        row = bytearray([0])  # filter: none
        for x in range(width):
            i = (y * width + x) * 4
            out = []
            for c in halves[i:i + 3]:
                if c != c or c in (float("inf"), float("-inf")):
                    stats[2] += 1
                    c = 0.0
                c = max(c, 0.0) * exposure
                stats[0] = max(stats[0], c)
                out.append(int(round(255.0 * (c / (1.0 + c)) ** (1.0 / 2.2))))
            stats[1] += sum(halves[i:i + 3]) / 3.0 if all(v == v for v in halves[i:i + 3]) else 0.0
            row += bytes(out)
        rows.append(bytes(row))

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
        width, height, pixels = read_dds(path)
        png, (peak, total, bad) = to_png(width, height, pixels, options.exposure)
        out = path[:-4] + ".png"
        with open(out, "wb") as f:
            f.write(png)
        print(f"{out}: {width}x{height}, peak {peak:.3f}, mean {total / (width * height):.4f}, non-finite {bad}")


if __name__ == "__main__":
    main()
