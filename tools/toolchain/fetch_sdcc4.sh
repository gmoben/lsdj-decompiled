#!/usr/bin/env bash
# Fetch a modern SDCC (4.6.0, Linux amd64) into toolchain/sdcc-4.6.0: what a
# fork's new C is compiled with (tools/match/sdcc4.py, docs/forking.md).
# LSDj's own C keeps the period compiler (build_sdcc221.sh).
set -o errexit
set -o pipefail

version=4.6.0
root=$(cd "$(dirname "$0")/../.." && pwd)
dest="$root/toolchain/sdcc-$version"
if [[ -x "$dest/bin/sdcc" ]]; then
    echo "$dest: already there"
    exit 0
fi
url="https://sourceforge.net/projects/sdcc/files/sdcc-linux-amd64/$version/sdcc-$version-amd64-unknown-linux2.5.tar.bz2/download"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -sSL -o "$tmp/sdcc.tar.bz2" "$url"
mkdir -p "$dest"
tar -xjf "$tmp/sdcc.tar.bz2" -C "$dest" --strip-components=1
"$dest/bin/sdcc" --version | head -1
