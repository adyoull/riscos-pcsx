#!/bin/bash
# Builds and runs the Fake RISC OS tests (32-bit ARM Linux, under QEMU).
#   tests/fake/run.sh SRC RCD_DIR
set -euo pipefail
SRC=$1; RCD=$2; HERE=$(cd "$(dirname "$0")" && pwd); OUT=${OUT:-/tmp/pcsx-fake}
FE=$SRC/frontend/riscos; F=$RCD/fake
mkdir -p "$OUT"
arm-linux-gnueabihf-gcc -g -O1 -Wall -funsigned-char -D__riscos -D__FAKE_RISCOS \
  -I"$F" -I"$FE" -no-pie -o "$OUT/test_plat" "$HERE/test_plat.c" "$FE/plat_riscos.c" "$F/riscos.c"
PATH=$RCD/toolchain/env/bin:$PATH "$RCD/qemu/aligntrap.sh" "$OUT/test_plat" 2>"$OUT/test_plat.log" | tail -1
grep -E "^FAIL" "$OUT/test_plat.log" && exit 1 || true
arm-linux-gnueabihf-gcc -g -O1 -Wall -funsigned-char -D__riscos -D__FAKE_RISCOS \
  -I"$F" -I"$FE" -I"$HERE/include" -I"$SRC/deps/libretro-common/include" -no-pie \
  -c -Dmain=wimp_main -o "$OUT/wimp.o" "$FE/wimp.c"
arm-linux-gnueabihf-gcc -g -O1 -Wall -funsigned-char -D__riscos -D__FAKE_RISCOS -I"$F" -I"$FE" -I"$HERE/include" -no-pie -c -o "$OUT/libwin.o" "$FE/libwin.c"
arm-linux-gnueabihf-gcc -g -O1 -Wall -funsigned-char -D__riscos -D__FAKE_RISCOS \
  -I"$F" -I"$FE" -I"$HERE/include" -no-pie -o "$OUT/test_wimp" "$HERE/test_wimp.c" \
  -I"$SRC/deps/libchdr/deps/miniz-3.1.2" -DMINIZ_NO_STDIO -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_ARCHIVE_APIS \
  "$OUT/wimp.o" "$OUT/libwin.o" "$FE/library.c" "$FE/archive.c" "$FE"/lzma/*.c \
  "$SRC/deps/libchdr/deps/miniz-3.1.2/miniz.c" "$FE/choices.c" "$FE/keys.c" "$F/riscos.c"
TEST_LOG=$OUT/test_wimp.log PATH=$RCD/toolchain/env/bin:$PATH "$RCD/qemu/aligntrap.sh" "$OUT/test_wimp" || true
tail -1 "$OUT/test_wimp.log"
grep -E "^FAIL" "$OUT/test_wimp.log" && exit 1 || true
