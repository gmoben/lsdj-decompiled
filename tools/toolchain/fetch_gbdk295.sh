#!/usr/bin/env bash
# Fetch the period-correct GBDK 2.95 toolchain used to build LSDj.
#
# GBDK 2.95 (Michael Hope, 2000) bundles SDCC 2.2.1 (gbz80 target). That exact
# compiler is the prerequisite for a *matching* C decompilation (Track 2). We
# pull it from the rbong/gbdk legacy mirror into a git-ignored toolchain/ dir.
#
# The binaries are 32-bit Windows PEs; run them with Wine (see tools/toolchain/sdcc295).
#
# Usage: tools/toolchain/fetch_gbdk295.sh [DEST_DIR]   (default: toolchain)
set -o errexit
set -o pipefail

REPO="https://github.com/rbong/gbdk.git"
SUBDIR="gbdk-2.95"
DEST="${1:-toolchain}"

if [[ -d "$DEST/$SUBDIR" ]]; then
    echo "==> $DEST/$SUBDIR already present; nothing to do."
    exit 0
fi

mkdir -p "$DEST"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "==> sparse-cloning $SUBDIR from $REPO ..."
git clone --no-checkout --depth 1 --filter=blob:none "$REPO" "$tmp/gbdk"
git -C "$tmp/gbdk" sparse-checkout set --no-cone "$SUBDIR"
git -C "$tmp/gbdk" checkout

cp -r "$tmp/gbdk/$SUBDIR" "$DEST/$SUBDIR"

echo "==> GBDK 2.95 installed at: $DEST/$SUBDIR"
echo "==> bundled binaries:"
ls -1 "$DEST/$SUBDIR/bin"
echo
echo "Next: verify the compiler runs:  tools/toolchain/sdcc295 --version"
