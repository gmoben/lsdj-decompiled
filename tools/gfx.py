#!/usr/bin/env python3
"""LSDj's tile blocks, and the PNGs the carve extracts them to.

A tile block (a `Tiles*` symbol) is what the tile loader reads
(LoadTilesToVram and CopyTileData in bank 0): a mode byte and a tile count,
a prefix the loader skips -- by the mode's low two bits: 1 a 128-byte
character map (the tile for each ASCII code), 2 none, otherwise 256 bytes --
then the tiles, raw 2bpp. Every block in 9.4.0 is mode 1.

The carve extracts a block's tiles to gfx/<name>.png and the build converts
it back with `rgbgfx -c embedded`: an indexed PNG whose palette is white,
light grey, dark grey, black for colours 0-3, sixteen tiles to a row (the
last row padded with colour-0 tiles, which the INCBIN's length leaves out).
The PNG is the ROM's data, extracted from your ROM and never committed.

A PNG is extracted only when it is missing, so an edited one survives every
carve and the build takes it (`make compare` then reports the difference);
delete it to extract it again. An edit keeps the image indexed with the four
palette entries in that order, and the block's tile count (its header, in
the carved asm) unless that is changed too.

    tools/gfx.py BLOCK.bin OUT.png     # a block's tiles as a PNG (for a look)
"""
from __future__ import annotations

import re
import struct
import sys
import zlib
from pathlib import Path

GFX_DIR = "gfx"                  # the PNGs the carve writes
BUILD_DIR = "build/gfx"          # the 2bpp rgbgfx makes of them (Makefile)
PALETTE = bytes([0xFF, 0xFF, 0xFF, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x00, 0x00, 0x00])
TILES_PER_ROW = 16


def prefix_size(mode: int) -> int:
    """Bytes the loader skips between a block's header and its tiles."""
    return {1: 0x80, 2: 0}.get(mode & 3, 0x100)


def block_size(block: bytes) -> int:
    """The size of the tile block that starts block (from its header)."""
    return 2 + prefix_size(block[0]) + 16 * block[1]


def block_tiles(block: bytes) -> bytes:
    """The block's tiles, 16 bytes each."""
    start = 2 + prefix_size(block[0])
    return block[start:start + 16 * block[1]]


def asset_name(label: str) -> str:
    """The file a block's tiles go to: TilesFontY2KHighlight -> font_y2k_highlight."""
    words = re.findall(r"[A-Z][a-z]+|[A-Z0-9]+(?![a-z])", label.removeprefix("Tiles"))
    return "_".join(w.lower() for w in words)


def tile_pixels(tiles: bytes) -> list[list[int]]:
    """Rows of colour indices, TILES_PER_ROW tiles wide."""
    count = len(tiles) // 16
    rows = (count + TILES_PER_ROW - 1) // TILES_PER_ROW
    image = [[0] * (8 * TILES_PER_ROW) for _ in range(8 * rows)]
    for i in range(count):
        left, top = 8 * (i % TILES_PER_ROW), 8 * (i // TILES_PER_ROW)
        for y in range(8):
            low, high = tiles[16 * i + 2 * y], tiles[16 * i + 2 * y + 1]
            for x in range(8):
                image[top + y][left + x] = (low >> (7 - x) & 1) | (high >> (7 - x) & 1) << 1
    return image


def png_bytes(image: list[list[int]]) -> bytes:
    """An indexed PNG, 2 bits a pixel, with PALETTE."""
    height, width = len(image), len(image[0])
    raw = bytearray()
    for row in image:
        raw.append(0)                                  # filter: none
        for x in range(0, width, 4):
            p = row[x:x + 4] + [0] * (4 - len(row[x:x + 4]))
            raw.append(p[0] << 6 | p[1] << 4 | p[2] << 2 | p[3])

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 2, 3, 0, 0, 0))
            + chunk(b"PLTE", PALETTE)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def extract_png(path: Path, tiles: bytes) -> str:
    """Write the tiles to path as a PNG unless a file is there: that one is
    yours (an edit, or this extraction), never overwritten. Returns
    "written", "same" (the file holds this extraction) or "differs"."""
    data = png_bytes(tile_pixels(tiles))
    if path.is_file():
        return "same" if path.read_bytes() == data else "differs"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return "written"


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    print(extract_png(Path(sys.argv[2]), block_tiles(Path(sys.argv[1]).read_bytes())))


if __name__ == "__main__":
    main()
