#!/bin/bash
# Relinks tests/headless against an already-built core in OUT/src.
set -e
OUT=$1; HERE=$(cd "$(dirname "$0")" && pwd)
cd "$OUT/src"; FE=frontend/riscos
arm-linux-gnueabihf-gcc -O2 -g -marm -mfpu=neon -mfloat-abi=hard -mno-unaligned-access -Wall -Wno-format-truncation -I deps/libretro-common/include -I $FE -no-pie -o "$OUT/headless" "$HERE/headless.c" $FE/host.c $FE/choices.c $FE/keys.c $FE/library.c pcsx_rearmed_libretro_riscos.a -lm -lpthread
