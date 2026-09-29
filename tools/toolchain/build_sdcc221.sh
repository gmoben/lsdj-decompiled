#!/usr/bin/env bash
# Build SDCC 2.2.1 (+ sdcpp, as-gbz80, link-gbz80) natively from the GBDK
# 2.95-2 sources -- the same compiler version as GBDK 2.95-3's sdcc.exe.
#
# Why: the Win32 sdcc.exe is linked with an old Boehm GC which, under Wine,
# frees live objects in larger translation units: garbage `.globl` names, and
# from ~30 functions on, NULL-pointer crashes (exit 5) that depend on the
# heap layout. Built with _NO_GC (the source's own switch), SDCC uses plain
# allocation -- zeroed, as the GC's was -- and runs natively, ~100x faster
# than through Wine. Validated: for every unit tried the native tools emit
# byte-identical asm, objects, listings and linked images to the Win32 ones
# (docs/toolchain.md); tools/match prefers them when present.
#
# Needs: git, gcc with 32-bit support (-m32; SDCC 2.2.1 assumes 32-bit
# pointers), bison, flex, make. Output: toolchain/sdcc-2.2.1-native/bin/.
#
# Source: https://juju.nz/src/juju/gbdk-releases (GBDK release tarballs as
# git), tag gbdk-2.95-2, commit 7ee938a8ae65290ddaf9ca234f2ef9777773c6b9.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="$ROOT/toolchain/sdcc-2.2.1-native"
REPO="https://juju.nz/src/juju/gbdk-releases"
TAG="gbdk-2.95-2"
COMMIT="7ee938a8ae65290ddaf9ca234f2ef9777773c6b9"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "==> fetching $REPO ($TAG)"
git clone -q --depth 1 --branch "$TAG" "$REPO" "$work/gbdk"
got="$(git -C "$work/gbdk" rev-parse HEAD)"
[ "$got" = "$COMMIT" ] || { echo "error: $TAG is $got, expected $COMMIT" >&2; exit 1; }
src="$work/gbdk/sdcc"
cd "$src"

echo "==> patching for a modern host (no codegen changes)"
python3 - <<'EOF'
from pathlib import Path

def edit(path, old, new, count=1):
    p = Path(path); s = p.read_text(errors="surrogateescape")
    assert s.count(old) >= 1, (path, old)
    p.write_text(s.replace(old, new, count), errors="surrogateescape")

# No Boehm GC. Its allocators return zeroed memory and SDCC relies on that.
for h in ("src/SDCCglobl.h", "src/SDCCset.h", "src/SDCChasht.h"):
    s = Path(h).read_text()
    s = s.replace("#define GC_malloc malloc", "#define GC_malloc(n) calloc(1, (n))")
    s = s.replace("#define GC_malloc_atomic malloc", "#define GC_malloc_atomic(n) calloc(1, (n))")
    Path(h).write_text(s)
edit("src/Makefile.in", "LIBS\t\t= -lgc -lm @LIBS@", "LIBS\t\t= -lm")
edit("src/Makefile.in", "all: ports checkconf $(LIBGC) $(TARGET)", "all: ports checkconf $(TARGET)")
# Modern flex defines yylineno itself and no longer has YY_PROTO.
edit("src/SDCC.lex", "int   yylineno = 1               ;", "/* yylineno: defined by flex */")
edit("src/SDCC.lex", "int yywrap YY_PROTO((void))", "int yywrap(void)")
# sdcpp: an unused GCC target declaration, and headers the 2000-era code
# got implicitly.
edit("support/cpp/i386/i386.h", "extern enum reg_class regclass_map[];",
     "/* extern enum reg_class regclass_map[]; */")
p = Path("support/cpp/cpplib.c"); s = p.read_text(errors="surrogateescape")
i = s.index("#include")
p.write_text(s[:i] + "#include <time.h>\n#include <stdlib.h>\n#include <string.h>\n" + s[i:],
             errors="surrogateescape")
EOF

CFLAGS="-O1 -g -w -fcommon -std=gnu89 -m32 -D_NO_GC -DYY_NO_UNISTD_H -D_POSIX_C_SOURCE=200112L -DHAVE_STRERROR"
echo "==> configure"
CC="gcc -m32" CFLAGS="$CFLAGS" ./configure > "$work/configure.log" 2>&1 || { tail -20 "$work/configure.log"; exit 1; }
sed -i "s|^CFLAGS          = .*|CFLAGS          = $CFLAGS|" Makefile.common

echo "==> build (sdcc, sdcpp, as-gbz80, link-gbz80)"
log="$work/build.log"
{
    make -C src CC="gcc -m32"       # the ports build in sub-makes; run twice
    make -C src CC="gcc -m32"
    make -C support/cpp CC="gcc -m32" CFLAGS="$CFLAGS"
    make -C as CC="gcc -m32"
    make -C link CC="gcc -m32"
} > "$log" 2>&1 || { tail -30 "$log"; exit 1; }

mkdir -p "$DEST/bin"
for t in sdcc sdcpp as-gbz80 link-gbz80; do
    [ -x "bin/$t" ] || { echo "error: bin/$t was not built (see $log)" >&2; tail -30 "$log"; exit 1; }
    cp "bin/$t" "$DEST/bin/$t"
done
echo "$COMMIT" > "$DEST/SOURCE_COMMIT"
"$DEST/bin/sdcc" --version 2>&1 | head -1
echo "==> installed to $DEST/bin"
