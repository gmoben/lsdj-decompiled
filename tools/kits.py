#!/usr/bin/env python3
"""LSDj's sample kits, and the WAVs the carve extracts their samples to.

A kit is a 16 KiB ROM bank (tools/gen_main.py emits it): at $4000 the sample
boundaries, 17 words -- sample i runs from word i to word i + 1, and the
words after the last sample's end are zero -- at $4022 sixteen 3-character
sample names ("\\0--" when unused), at $4052 the kit's 6-character name, at
$4058 eight bytes the kit tools write (flags), from $4060 the samples, then
$FF to the end of the bank. A sample is 4-bit at 11468 Hz, two to a byte,
the high nibble first (as the wave channel plays them).

The carve extracts each sample to data/kits/<kit>/<NN>_<name>.wav (8-bit
mono, a 4-bit value v as v * 17) and the build converts it back: the top
four bits of each sample of the first channel (8- or 16-bit; no resampling,
so a new sample is prepared at 11468 Hz). A WAV is extracted only when
missing, so an edited one is what the build uses (delete it to extract it
again). The kit's boundary words are the samples' labels, so a sample can
change length; the bank's free space takes up the difference. The WAVs are
the ROM's data: extracted from your ROM, never committed.

    tools/kits.py pcm IN.wav OUT.pcm    # a WAV as the kit's 4-bit samples
"""
from __future__ import annotations

import io
import struct
import sys
import wave
from pathlib import Path

RATE = 11468
KITS_DIR = "data/kits"           # the WAVs the carve writes
BUILD_DIR = "build/kits"         # the 4-bit samples made of them (Makefile)
SAMPLES = 0x4060                 # where a kit's samples start


def boundaries(bank: bytes) -> list[int] | None:
    """The sample boundaries of a kit bank (CPU addresses, the first $4060,
    one more than the samples), or None unless the bank is a kit laid out
    as above: increasing boundaries, zeros after, $FF after the samples."""
    if bank[:2] != b"\x60\x40":
        return None
    words = [bank[2 * i] | bank[2 * i + 1] << 8 for i in range(17)]
    n = words.index(0) - 1 if 0 in words else 16
    if n < 1 or any(words[i] >= words[i + 1] for i in range(n)) or any(words[n + 1:]):
        return None
    end = words[n] - 0x4000
    if end > len(bank) or bank[end:] != b"\xff" * (len(bank) - end):
        return None
    return words[:n + 1]


def wav_bytes(samples: bytes) -> bytes:
    """A kit sample (4-bit, packed) as a WAV file: 8-bit mono at RATE."""
    frames = bytes(17 * v for b in samples for v in (b >> 4, b & 15))
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(RATE)
        w.writeframes(frames)
    return buffer.getvalue()


def pcm_bytes(path: Path) -> bytes:
    """A WAV file as a kit sample: the top four bits of each sample of the
    first channel, two to a byte (an odd count ends on the midpoint, 8)."""
    with wave.open(str(path), "rb") as w:
        channels, width = w.getnchannels(), w.getsampwidth()
        raw = w.readframes(w.getnframes())
    step = channels * width
    if width == 1:
        values = [raw[i] for i in range(0, len(raw), step)]
    elif width == 2:
        values = [(struct.unpack_from("<h", raw, i)[0] + 0x8000) >> 8 for i in range(0, len(raw), step)]
    else:
        raise ValueError(f"{path}: {8 * width}-bit samples (use 8- or 16-bit)")
    nibbles = [v >> 4 for v in values] + [8] * (len(values) % 2)
    return bytes(nibbles[i] << 4 | nibbles[i + 1] for i in range(0, len(nibbles), 2))


def extract_wav(path: Path, samples: bytes) -> str:
    """Write the sample to path as a WAV unless a file is there: that one is
    yours (an edit, or this extraction), never overwritten. Returns
    "written", "same" (the file holds this extraction) or "differs"."""
    data = wav_bytes(samples)
    if path.is_file():
        return "same" if path.read_bytes() == data else "differs"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return "written"


def main() -> None:
    if len(sys.argv) != 4 or sys.argv[1] != "pcm":
        sys.exit(__doc__)
    out = Path(sys.argv[3])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(pcm_bytes(Path(sys.argv[2])))


if __name__ == "__main__":
    main()
