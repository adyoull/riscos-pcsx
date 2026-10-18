#!/bin/bash
# Copies changed sources from SRC into OUT/src, rebuilds the core there
# (incrementally) and relinks the headless test.
set -euo pipefail
SRC=$1; OUT=$2; DRC=${3:-ari64}; HERE=$(cd "$(dirname "$0")" && pwd)
(cd "$SRC" && tar cf - --exclude=.git --exclude="*.o" --exclude="*.d" --exclude="*.a" .) | tar xf - -C "$OUT/src" --keep-newer-files 2>/dev/null || true
cd "$OUT/src"
make -f Makefile.libretro platform=riscos CROSS=arm-linux-gnueabihf- RISCOS_DYNAREC=$DRC NEON_ALIGN=${NEON_ALIGN:-/home/claude/riscos-crossdev/checks/neon-align.py} -j"$(nproc)" >"$OUT/core-build.log" 2>&1 || { grep -iE " error|error:" "$OUT/core-build.log" | head; exit 1; }
"$HERE/quick.sh" "$OUT" 2>&1 | grep -iE " error|error:" || true
