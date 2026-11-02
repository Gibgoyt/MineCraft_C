#!/usr/bin/env python3
"""Writes the compile_png fixtures, and checks each one against decoders independent of ours.

Not part of the build. The output is committed, so tests need neither Python nor ImageMagick. Re-run only to change
the set, from any directory:

    python3 -I tests/compile/png/fixtures/generate.py

Most files come from the small encoder here, which exercises every color type, depth, filter and interlace choice, with
the expected pixels worked out directly from the samples it wrote. ImageMagick must then agree on every expected image.
The rest come from third-party encoders, Pillow and ImageMagick.

manifest.txt has one line per file: name, width, height, bit depth, color type, interlaced, has alpha, and FNV-1a 64 of
the expected RGBA8 bytes, top row first.
"""

import io
import pathlib
import random
import struct
import subprocess
import zlib

HERE = pathlib.Path(__file__).resolve().parent
RNG = random.Random(20261102)
ADAM7 = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def fnv1a64(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def chunk(kind, data=b""):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def to8(sample, depth):
    if depth == 16:
        return (sample * 255 + 32767) // 65535
    return sample * 255 // ((1 << depth) - 1)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def filter_row(row, previous, kind, bpp):
    out = bytearray([kind])
    for i, value in enumerate(row):
        a = row[i - bpp] if i >= bpp else 0
        b = previous[i] if previous else 0
        c = previous[i - bpp] if previous and i >= bpp else 0
        predicted = {0: 0, 1: a, 2: b, 3: (a + b) // 2, 4: paeth(a, b, c)}[kind]
        out.append((value - predicted) & 255)
    return out


def pack_row(pixels, depth):
    """One row of pixels, each a tuple of samples, to bytes."""
    out = bytearray()
    bits = 0
    held = 0
    for pixel in pixels:
        for sample in pixel:
            if depth == 16:
                out += struct.pack(">H", sample)
            elif depth == 8:
                out.append(sample)
            else:
                held = (held << depth) | sample
                bits += depth
                if bits == 8:
                    out.append(held)
                    held = bits = 0
    if bits:
        out.append(held << (8 - bits))
    return bytes(out)


def encode_image(samples, width, height, depth, color, interlaced, filters):
    """samples[y][x] is a tuple. filters(row_number) gives a row's filter type."""
    bpp = max(1, CHANNELS[color] * depth // 8)
    raw = bytearray()
    passes = ADAM7 if interlaced else [(0, 0, 1, 1)]
    count = 0
    for sx, sy, dx, dy in passes:
        previous = None
        for y in range(sy, height, dy):
            row_pixels = [samples[y][x] for x in range(sx, width, dx)]
            if not row_pixels:
                continue
            row = pack_row(row_pixels, depth)
            raw += filter_row(row, previous, filters(count), bpp)
            previous = row
            count += 1
    return bytes(raw)


def make_samples(width, height, depth, color):
    top = (1 << depth) - 1
    channels = CHANNELS[color]
    # Mostly smooth with some noise, so that zlib finds matches and every filter has real work.
    rows = []
    for y in range(height):
        row = []
        for x in range(width):
            pixel = []
            for c in range(channels):
                wave = (x * (c + 3) + y * (c + 5)) * top // (width + height + 7)
                value = RNG.randrange(top + 1) if RNG.randrange(4) == 0 else wave & top
                pixel.append(value)
            row.append(tuple(pixel))
        rows.append(row)
    return rows


def make_palette(depth, entries=None):
    count = entries if entries is not None else 1 << depth
    return [(i * 37 & 255, i * 91 & 255, 255 - i & 255) for i in range(count)]


def expected_rgba(samples, depth, color, palette, trns):
    out = bytearray()
    for row in samples:
        for pixel in row:
            if color == 3:
                r, g, b = palette[pixel[0]]
                a = trns[pixel[0]] if trns and pixel[0] < len(trns) else 255
                out += bytes((r, g, b, a))
            elif color == 0:
                v = to8(pixel[0], depth)
                out += bytes((v, v, v, 0 if trns is not None and pixel[0] == trns else 255))
            elif color == 4:
                v = to8(pixel[0], depth)
                out += bytes((v, v, v, to8(pixel[1], depth)))
            elif color == 2:
                a = 0 if trns is not None and tuple(pixel) == tuple(trns) else 255
                out += bytes((to8(pixel[0], depth), to8(pixel[1], depth), to8(pixel[2], depth), a))
            else:
                out += bytes(to8(s, depth) for s in pixel)
    return bytes(out)


def magick_rgba(path):
    """ImageMagick's decode to RGBA8. Run on our own generated files only."""
    result = subprocess.run(
        ["timeout", "60", "magick", f"png:{path}", "-depth", "8", "rgba:-"], capture_output=True, check=True
    )
    return result.stdout


MANIFEST = []


def record(name, width, height, depth, color, interlaced, has_alpha, rgba):
    assert len(rgba) == width * height * 4
    MANIFEST.append(f"{name}.png {width} {height} {depth} {color} {int(interlaced)} {int(has_alpha)} {fnv1a64(rgba):016x}")


def write_image(
    name,
    width,
    height,
    depth,
    color,
    interlaced=False,
    filters=None,
    palette_entries=None,
    trns_mode=None,
    idat_split=None,
    extras_before=(),
    extras_after=(),
    level=9,
):
    samples = make_samples(width, height, depth, color)
    palette = None
    trns = None
    pre = b""
    if color == 3:
        palette = make_palette(depth, palette_entries)
        limit = len(palette)
        samples = [[(p[0] % limit,) for p in row] for row in samples]
        pre += chunk(b"PLTE", bytes(v for e in palette for v in e))
    if trns_mode == "palette":
        trns = [RNG.randrange(256) for _ in range(max(1, len(palette) // 2))]
        pre += chunk(b"tRNS", bytes(trns))
    elif trns_mode == "gray":
        trns = samples[height // 2][width // 2][0]
        pre += chunk(b"tRNS", struct.pack(">H", trns))
    elif trns_mode == "rgb":
        trns = samples[height // 2][width // 2]
        pre += chunk(b"tRNS", struct.pack(">HHH", *trns))
    if filters is None:
        start = RNG.randrange(5)
        filters = lambda row: (row + start) % 5
    raw = encode_image(samples, width, height, depth, color, interlaced, filters)
    stream = zlib.compress(raw, level)
    if idat_split:
        idats = chunk(b"IDAT")
        for i in range(0, len(stream), idat_split):
            idats += chunk(b"IDAT", stream[i : i + idat_split]) + chunk(b"IDAT")
    else:
        idats = chunk(b"IDAT", stream)
    header = struct.pack(">IIBBBBB", width, height, depth, color, 0, 0, int(interlaced))
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
    for extra in extras_before:
        data += extra
    data += pre + idats
    for extra in extras_after:
        data += extra
    data += chunk(b"IEND")
    path = HERE / f"{name}.png"
    path.write_bytes(data)
    rgba = expected_rgba(samples, depth, color, palette, trns)
    # A second opinion: any disagreement stops the generator rather than committing a wrong expectation.
    theirs = magick_rgba(path)
    assert theirs == rgba, f"{name}: ImageMagick disagrees with the generator"
    has_alpha = color in (4, 6) or trns is not None
    record(name, width, height, depth, color, interlaced, has_alpha, rgba)


def third_party():
    from PIL import Image

    width, height = 48, 32
    rgba = bytearray()
    for y in range(height):
        for x in range(width):
            rgba += bytes((x * 5 & 255, y * 7 & 255, (x + y) * 3 & 255, 255 - x * 4 & 255))
    image = Image.frombytes("RGBA", (width, height), bytes(rgba))

    buffer = io.BytesIO()
    image.save(buffer, "PNG", optimize=True)
    (HERE / "pillow_rgba.png").write_bytes(buffer.getvalue())
    record("pillow_rgba", width, height, 8, 6, False, True, bytes(rgba))

    paletted = image.convert("RGB").quantize(16)
    buffer = io.BytesIO()
    paletted.save(buffer, "PNG", bits=4)
    (HERE / "pillow_palette.png").write_bytes(buffer.getvalue())
    record("pillow_palette", width, height, 4, 3, False, False, paletted.convert("RGBA").tobytes())

    gray = image.convert("L")
    buffer = io.BytesIO()
    gray.save(buffer, "PNG")
    (HERE / "pillow_gray.png").write_bytes(buffer.getvalue())
    record("pillow_gray", width, height, 8, 0, False, False, gray.convert("RGBA").tobytes())

    for name, spec, size, depth, color, interlaced, alpha in (
        ("magick_rgb48", "PNG48", "40x30", 16, 2, False, False),
        ("magick_rgba64", "PNG64", "40x30", 16, 6, False, True),
        ("magick_rgb24_interlaced", "PNG24", "33x21", 8, 2, True, False),
    ):
        path = HERE / f"{name}.png"
        command = ["timeout", "60", "magick", "-seed", "7", "-size", size, "gradient:#102030-#f0e0d0"]
        if alpha:
            command += ["-alpha", "set", "-channel", "A", "-evaluate", "set", "60%", "+channel"]
        if interlaced:
            command += ["-interlace", "PNG"]
        command += ["-define", "png:exclude-chunks=date,time", f"{spec}:{path}"]
        subprocess.run(command, check=True)
        width, height = (int(v) for v in size.split("x"))
        record(name, width, height, depth, color, interlaced, alpha, magick_rgba(path))


def main():
    for path in HERE.glob("*.png"):
        path.unlink()

    for color, depths in ((0, (1, 2, 4, 8, 16)), (2, (8, 16)), (3, (1, 2, 4, 8)), (4, (8, 16)), (6, (8, 16))):
        for depth in depths:
            for interlaced in (False, True):
                suffix = "_adam7" if interlaced else ""
                write_image(f"c{color}_d{depth}{suffix}", 13, 11, depth, color, interlaced)

    write_image("trns_gray1", 13, 11, 1, 0, trns_mode="gray")
    write_image("trns_gray8", 13, 11, 8, 0, trns_mode="gray")
    write_image("trns_gray16", 13, 11, 16, 0, trns_mode="gray")
    write_image("trns_rgb8", 13, 11, 8, 2, trns_mode="rgb")
    write_image("trns_rgb16", 13, 11, 16, 2, trns_mode="rgb")
    write_image("trns_palette4", 13, 11, 4, 3, trns_mode="palette")
    write_image("trns_palette8_adam7", 13, 11, 8, 3, interlaced=True, trns_mode="palette")
    write_image("short_palette", 13, 11, 4, 3, palette_entries=5)

    # Adam7 at sizes where some passes are empty, at byte and at sub-byte depths.
    for width, height in ((1, 1), (2, 1), (1, 2), (3, 3), (4, 4), (5, 5), (8, 8), (9, 9), (1, 9), (9, 1)):
        write_image(f"adam7_{width}x{height}_rgba8", width, height, 8, 6, interlaced=True)
    for width, height in ((1, 1), (3, 2), (5, 3), (2, 7)):
        write_image(f"adam7_{width}x{height}_gray1", width, height, 1, 0, interlaced=True)

    # One filter on every row, including the first, where "above" reads as zero.
    for kind in range(5):
        write_image(f"filter{kind}_rgba8", 12, 9, 8, 6, filters=lambda row, k=kind: k)
        write_image(f"filter{kind}_gray2", 12, 9, 2, 0, filters=lambda row, k=kind: k)
        write_image(f"filter{kind}_rgb16", 12, 9, 16, 2, filters=lambda row, k=kind: k)

    # Structure a decoder may meet in the wild.
    write_image("idat_split", 24, 16, 8, 6, idat_split=7)
    write_image(
        "ancillary",
        12,
        9,
        8,
        2,
        extras_before=(chunk(b"gAMA", struct.pack(">I", 45455)), chunk(b"tEXt", b"Title\0ours"), chunk(b"xxXx", b"\1\2")),
        extras_after=(chunk(b"tIME", b"\x07\xea\x0b\x02\0\0\0"), chunk(b"zzZz")),
    )
    write_image(
        "suggested_palette",
        12,
        9,
        8,
        6,
        extras_before=(chunk(b"PLTE", bytes(range(30))),),
    )
    write_image("stored_deflate", 12, 9, 8, 6, level=0)
    write_image("large_rgb8", 200, 150, 8, 2)
    write_image("large_rgba8_adam7", 150, 90, 8, 6, interlaced=True)

    third_party()
    (HERE / "manifest.txt").write_text("\n".join(MANIFEST) + "\n")


main()
