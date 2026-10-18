#!/bin/bash
# Builds the core and tests/headless for arm-linux-gnueabihf (the RISC OS
# compiler flags as near as Linux allows: ARM, NEON, no unaligned access).
#   run-build.sh SRC OUT [DYNAREC]     DYNAREC = ari64 (default) or none
set -euo pipefail
SRC=$1; OUT=$2; DRC=${3:-ari64}; HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
rm -rf "$OUT/src" && mkdir -p "$OUT/src" && (cd "$SRC" && tar cf - --exclude=.git --exclude="*.o" --exclude="*.d" --exclude="*.a" .) | tar xf - -C "$OUT/src"
cd "$OUT/src"
make -f Makefile.libretro platform=riscos CROSS=arm-linux-gnueabihf- RISCOS_DYNAREC=$DRC NEON_ALIGN=${NEON_ALIGN:-/home/claude/riscos-crossdev/checks/neon-align.py} \
  -j"$(nproc)" >"$OUT/core-build.log" 2>&1 || { tail -20 "$OUT/core-build.log"; exit 1; }
FE=frontend/riscos
CF="-O2 -g -marm -mfpu=neon -mfloat-abi=hard -mno-unaligned-access -Wall -I deps/libretro-common/include -I $FE"
arm-linux-gnueabihf-gcc $CF -no-pie -o "$OUT/headless" \
  "$HERE/headless.c" $FE/host.c $FE/choices.c $FE/keys.c $FE/library.c \
  pcsx_rearmed_libretro_riscos.a -lm -lpthread
echo "built $OUT/headless"
