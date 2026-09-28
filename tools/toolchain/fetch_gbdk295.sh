#!/usr/bin/env bash
# Fetch the period-correct GBDK toolchain used to build LSDj.
#
# GBDK 2.95-3 (Michael Hope, 19 Aug 2000) is the last GBDK release built on
# SDCC 2.2.1 (gbz80 target) and the one whose defaults match LSDj's code: the
# small memory model (direct calls, arguments at sp+4) is the default, where
# plain 2.95 assumed banked calls unless a function was marked `nonbanked`.
# That compiler, plus the peephole rules in tools/match/lsdj.peep, reproduces
# LSDj functions byte-for-byte (docs/toolchain.md). We download the Win32
# binary release from SourceForge into a git-ignored toolchain/ dir.
#
# The binaries are 32-bit Windows PEs; run them with Wine (see tools/toolchain/sdcc295).
# The libc/ sources in the same tree are what tools/match resolves GBDK's
# register names from (libc/gb/sfr.s).
#
# For comparison work, plain GBDK 2.95 is mirrored at
# https://github.com/rbong/gbdk (gbdk-2.95/, with binaries); point GBDK_295 at
# it to use it instead.
#
# Usage: tools/toolchain/fetch_gbdk295.sh [DEST_DIR]   (default: toolchain)
set -o errexit
set -o pipefail

URL="https://sourceforge.net/projects/gbdk/files/gbdk-win32/2.95-3/gbdk-2.95-3-win32.zip/download"
ZIP_SHA1="d9f412e00adf28f1f7e09c2de5c437d4daabb259"
SUBDIR="gbdk-2.95-3"
DEST="${1:-toolchain}"

if [[ -d "$DEST/$SUBDIR" ]]; then
    echo "==> $DEST/$SUBDIR already present; nothing to do."
    exit 0
fi
for tool in curl unzip; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: $tool is required." >&2
        exit 1
    fi
done

mkdir -p "$DEST"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "==> downloading $SUBDIR (Win32 release) from SourceForge ..."
curl -sSL --max-time 300 -o "$tmp/gbdk.zip" "$URL"
printf '%s  %s\n' "$ZIP_SHA1" "$tmp/gbdk.zip" | sha1sum -c - >/dev/null || {
    echo "error: checksum mismatch on the downloaded zip (got $(sha1sum "$tmp/gbdk.zip" | cut -c1-40))." >&2
    exit 1
}
unzip -q "$tmp/gbdk.zip" -d "$tmp/unzipped"
# The zip unpacks to a top-level gbdk/ directory.
mv "$tmp/unzipped/gbdk" "$DEST/$SUBDIR"

echo "==> GBDK 2.95-3 installed at: $DEST/$SUBDIR"
echo "==> bundled binaries:"
ls -1 "$DEST/$SUBDIR/bin"
echo
echo "Next: verify the compiler runs:  tools/toolchain/sdcc295 --version"
