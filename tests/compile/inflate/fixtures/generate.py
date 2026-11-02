#!/usr/bin/env python3
"""Writes the compile_inflate fixtures: real streams from zlib, an encoder independent of the decoder under test.

Not part of the build. The output is committed, so tests need neither Python nor zlib. Re-run only to change the
corpus, from any directory:

    python3 -I tests/compile/inflate/fixtures/generate.py

manifest.txt has one line per stream: file, wrapper (zlib or raw), decoded length, FNV-1a 64 of the decoded bytes, and
the type of its first block.
"""

import pathlib
import random
import zlib

HERE = pathlib.Path(__file__).resolve().parent
RNG = random.Random(20261102)


def fnv1a64(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def noise(count):
    return bytes(RNG.randrange(256) for _ in range(count))


def text(count):
    words = b"dirt stone water sand gravel oak leaves coal iron gold redstone diamond torch chest furnace ".split()
    out = bytearray()
    while len(out) < count:
        out += RNG.choice(words) + b" "
    return bytes(out[:count])


def far_match():
    """A match at exactly the largest distance, 32768."""
    head = noise(300)
    return head + bytes(32768 - 300) + head


def pixel_rows():
    """Image-like data, with structure at many distances."""
    out = bytearray()
    for y in range(64):
        for x in range(64):
            out += bytes(((x * 4 + y) & 255, (y * 4) & 255, (x ^ y) & 255, 255))
    return bytes(out)


PAYLOADS = {
    "empty": b"",
    "one": b"A",
    "text": text(8000),
    "zeros": bytes(70000),
    "noise": noise(1500),
    "all_bytes": bytes(range(256)) * 8,
    "far_match": far_match(),
    "pixels": pixel_rows(),
}

ENCODINGS = {
    "stored": (0, zlib.Z_DEFAULT_STRATEGY),
    "fast": (1, zlib.Z_DEFAULT_STRATEGY),
    "default": (6, zlib.Z_DEFAULT_STRATEGY),
    "best": (9, zlib.Z_DEFAULT_STRATEGY),
    "fixed": (9, zlib.Z_FIXED),
    "huffman": (9, zlib.Z_HUFFMAN_ONLY),
    "rle": (9, zlib.Z_RLE),
    "filtered": (9, zlib.Z_FILTERED),
}


def compress(data, level, strategy, wbits):
    encoder = zlib.compressobj(level, zlib.DEFLATED, wbits, 9, strategy)
    return encoder.compress(data) + encoder.flush()


def flushed_blocks(data):
    """Several blocks in one stream, with the empty stored blocks a flush writes between them."""
    encoder = zlib.compressobj(6, zlib.DEFLATED, 15)
    out = bytearray()
    third = len(data) // 3
    out += encoder.compress(data[:third]) + encoder.flush(zlib.Z_SYNC_FLUSH)
    out += encoder.compress(data[third : third * 2]) + encoder.flush(zlib.Z_FULL_FLUSH)
    out += encoder.compress(data[third * 2 :]) + encoder.flush()
    return bytes(out)


def first_block_type(stream, wrapper):
    return (stream[2 if wrapper == "zlib" else 0] >> 1) & 3


def main():
    lines = []

    def write(name, wrapper, stream, decoded):
        # The encoder's own decoder has to agree before a stream becomes a fixture.
        assert zlib.decompress(stream, 15 if wrapper == "zlib" else -15) == decoded
        suffix = "zlib" if wrapper == "zlib" else "deflate"
        file_name = f"{name}.{suffix}"
        (HERE / file_name).write_bytes(stream)
        lines.append(f"{file_name} {wrapper} {len(decoded)} {fnv1a64(decoded):016x} {first_block_type(stream, wrapper)}")

    for payload_name, payload in PAYLOADS.items():
        for encoding_name, (level, strategy) in ENCODINGS.items():
            if payload_name == "noise" and encoding_name not in ("stored", "best"):
                continue  # incompressible, so each copy is 1.5 KiB and shows nothing new
            if payload_name == "far_match" and encoding_name not in ("best", "fixed"):
                continue  # the other strategies do not make the far match this is here for
            name = f"{payload_name}_{encoding_name}"
            write(name, "zlib", compress(payload, level, strategy, 15), payload)
    write("text_raw", "raw", compress(PAYLOADS["text"], 9, zlib.Z_DEFAULT_STRATEGY, -15), PAYLOADS["text"])
    write("one_raw", "raw", compress(PAYLOADS["one"], 9, zlib.Z_DEFAULT_STRATEGY, -15), PAYLOADS["one"])
    write("text_window512", "zlib", compress(PAYLOADS["text"], 9, zlib.Z_DEFAULT_STRATEGY, 9), PAYLOADS["text"])
    write("text_flushed", "zlib", flushed_blocks(PAYLOADS["text"]), PAYLOADS["text"])
    (HERE / "manifest.txt").write_text("\n".join(lines) + "\n")


main()
