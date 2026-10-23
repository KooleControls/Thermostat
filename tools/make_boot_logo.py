"""Turn a PNG into the boot logo the firmware draws before LVGL exists.

    python tools/make_boot_logo.py main/Application/DisplayManager/images/boot_logo.png \
        main/Application/DisplayManager/images/boot_logo.c --width 360 \
        --pitch-x-nm 149708 --pitch-y-nm 146208 --bg FFFFFF

Writes a C file holding the logo as run-length-encoded RGB565, composited over
--bg and scaled to --width panel pixels. The height follows from the image's
aspect *and the panel's pixel pitch*: this panel's pixels are not square (see
LCD_ACTIVE_W_UM in BoardConfig.h), and a bitmap scaled as if they were comes
out squashed. Pass the pitches Board::GetPixelPitchXNm/YNm return.

Standard library only (zlib + struct), so it runs wherever Python does. Reads
8-bit, non-interlaced RGB or RGBA PNGs, which is what a logo export is.
"""
import argparse
import struct
import zlib


def read_png(path):
    """Return (width, height, rows) with rows as lists of (r, g, b, a) tuples."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG")

    pos, idat, ihdr = 8, b"", None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
        pos += 12 + length

    width, height, depth, ctype, _, _, interlace = ihdr
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise SystemExit(f"{path}: need 8-bit non-interlaced RGB/RGBA "
                         f"(have depth {depth}, colour type {ctype}, interlace {interlace})")
    bpp = 4 if ctype == 6 else 3
    stride = width * bpp
    raw = zlib.decompress(idat)

    rows, prev = [], bytearray(stride)
    for y in range(height):
        base = y * (stride + 1)
        kind = raw[base]
        line = bytearray(raw[base + 1:base + 1 + stride])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if kind == 1:
                line[i] = (line[i] + a) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + b) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        prev = line
        rows.append([tuple(line[x * bpp:x * bpp + bpp]) + ((255,) if bpp == 3 else ())
                     for x in range(width)])
    return width, height, rows


def footprint(n_src, n_dst):
    """Per output index, the (source index, weight) pairs of a box filter."""
    scale = n_src / n_dst
    out = []
    for d in range(n_dst):
        lo, hi = d * scale, (d + 1) * scale
        taps, s = [], int(lo)
        while s < hi and s < n_src:
            w = min(hi, s + 1) - max(lo, s)
            if w > 0:
                taps.append((s, w))
            s += 1
        out.append(taps)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("png")
    ap.add_argument("out_c")
    ap.add_argument("--width", type=int, required=True, help="logo width on the panel, px")
    ap.add_argument("--pitch-x-nm", type=int, required=True)
    ap.add_argument("--pitch-y-nm", type=int, required=True)
    ap.add_argument("--bg", default="FFFFFF", help="background, RRGGBB")
    args = ap.parse_args()

    bg = tuple(int(args.bg[i:i + 2], 16) for i in (0, 2, 4))
    sw, sh, src = read_png(args.png)

    dw = args.width
    # Physical height / physical width must match the source's aspect.
    dh = round(dw * sh / sw * args.pitch_x_nm / args.pitch_y_nm)

    # Composite over the background first, so edges blend toward it.
    flat = [[tuple((p[k] * p[3] + bg[k] * (255 - p[3])) / 255 for k in range(3))
             for p in row] for row in src]

    cols, rws = footprint(sw, dw), footprint(sh, dh)
    pixels = []
    for taps_y in rws:
        for taps_x in cols:
            acc, total = [0.0, 0.0, 0.0], 0.0
            for sy, wy in taps_y:
                row = flat[sy]
                for sx, wx in taps_x:
                    w = wy * wx
                    p = row[sx]
                    acc[0] += p[0] * w
                    acc[1] += p[1] * w
                    acc[2] += p[2] * w
                    total += w
            r, g, b = (round(v / total) for v in acc)
            pixels.append(((r * 31 + 127) // 255) << 11 |
                          ((g * 63 + 127) // 255) << 5 |
                          ((b * 31 + 127) // 255))

    runs = []
    for px in pixels:
        if runs and runs[-1][1] == px and runs[-1][0] < 0xFFFF:
            runs[-1][0] += 1
        else:
            runs.append([1, px])

    bg565 = ((bg[0] * 31 + 127) // 255) << 11 | ((bg[1] * 63 + 127) // 255) << 5 | ((bg[2] * 31 + 127) // 255)
    words = [v for run in runs for v in run]
    body = ",\n".join("    " + ", ".join(f"0x{w:04X}" for w in words[i:i + 12])
                      for i in range(0, len(words), 12))

    with open(args.out_c, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"""// Generated by tools/make_boot_logo.py — rerun it, do not edit.
//   source  {args.png.replace(chr(92), '/')} ({sw} x {sh})
//   drawn   {dw} x {dh} panel px over #{args.bg.upper()}, pixel pitch {args.pitch_x_nm} x {args.pitch_y_nm} nm
//   stored  {len(runs)} runs of (count, RGB565), row-major

#include "BootLogo.h"

static const uint16_t kRuns[] = {{
{body}
}};

const BootLogo kBootLogo = {{ {dw}, {dh}, 0x{bg565:04X}, {len(runs)}, kRuns }};
""")
    print(f"{dw} x {dh}, {len(runs)} runs, {len(words) * 2} bytes")


if __name__ == "__main__":
    main()
