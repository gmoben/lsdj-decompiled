# LSDj 9.4.0 ROM map

Living document of reverse-engineering findings. Add to it as routines, data
tables, and memory regions are identified and carved out of `src/main.asm`.

## Cartridge identity

| Field | Value | Notes |
|-------|-------|-------|
| Title | `LSDj-v9.4.0` | `0x134`-`0x142` |
| sha1 | `58ba2e44c726c61d8c91423ec82152319dddc2ca` | full ROM |
| md5 | `b4c353122033f538e88eb00c083c0572` | full ROM |
| Size | 1 MiB (1,048,576 bytes) | 64 x 16 KiB banks |

## Cartridge header (`0x100`-`0x14F`)

| Offset | Bytes | Meaning |
|--------|-------|---------|
| `0x100` | `00 C3 50 01` | entry: `nop` / `jp $0150` |
| `0x104`-`0x133` | Nintendo logo | standard |
| `0x134`-`0x142` | `LSDj-v9.4.0` | title (null padded) |
| `0x143` | `80` | CGB flag: CGB-enhanced, DMG-compatible |
| `0x144`-`0x145` | `30 30` ("00") | new licensee code |
| `0x146` | `00` | SGB flag: none |
| `0x147` | `1B` | cart type: **MBC5 + RAM + BATTERY** |
| `0x148` | `05` | ROM size: 1 MiB (64 banks) |
| `0x149` | `04` | RAM size: 128 KiB (16 x 8 KiB) |
| `0x14A` | `00` | destination: Japanese (non-region-specific) |
| `0x14B` | `33` | old licensee: use new code |
| `0x14C` | `01` | mask ROM version |
| `0x14D` | `C6` | header checksum |
| `0x14E`-`0x14F` | `55 C4` | global checksum |

Execution begins at `0x150` (the `jp` target from the entry point).

## Memory layout (MBC5)

| Region | Address | Notes |
|--------|---------|-------|
| ROM bank 0 | `0x0000`-`0x3FFF` | fixed; header, entry, vectors |
| ROM bank X | `0x4000`-`0x7FFF` | switchable (banks `$001`-`$03F`) |
| VRAM | `0x8000`-`0x9FFF` | |
| SRAM | `0xA000`-`0xBFFF` | 128 KiB battery-backed; LSDj save data |
| WRAM | `0xC000`-`0xDFFF` | |
| OAM | `0xFE00`-`0xFE9F` | |
| HRAM | `0xFF80`-`0xFFFE` | |

## Toolchain origin

LSDj is written in C and built with a **modified GBDK 2.95** (see References in
`README.md`). Expect compiler-emitted patterns: the SDCC/GBDK calling
convention, `__sdcc`-style runtime helpers, banked-call trampolines, and library
routines from `crt0`/`libc`. Recognising these idioms is the fastest way to
label large stretches of the disassembly.

## Bank map (to be filled in)

| Bank | Contents | Status |
|------|----------|--------|
| `$000` | entry, header, vectors, core dispatch | INCBIN (not yet carved) |
| `$001`-`$03F` | code, data, graphics, sample kits, ... | INCBIN (not yet carved) |
