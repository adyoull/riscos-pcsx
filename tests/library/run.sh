#!/bin/bash
# Builds made-up disc images and runs test_library (Linux, host gcc).
#   tests/library/run.sh SRC
set -euo pipefail
SRC=$1; HERE=$(cd "$(dirname "$0")" && pwd); T=${T:-/tmp/libtest}
rm -rf "$T" && mkdir -p "$T/discs/sub" "$T/covers" "$T/elsewhere" "$T/states/Game_One"
mkiso() {   # mkiso OUT SERIAL_PATH
  local d; d=$(mktemp -d)
  printf 'BOOT = cdrom:\\%s;1\r\nTCB = 4\r\n' "$2" > "$d/SYSTEM.CNF"
  printf 'x' > "$d/$(echo "$2" | tr -d ';')"
  genisoimage -quiet -V TEST -o "$1" "$d"; rm -rf "$d"
}
mkiso "$T/game1.iso" SLES_123.45
python3 - "$T/game1.iso" "$T/discs/Game One.bin" <<'PY'
import sys
iso=open(sys.argv[1],'rb').read(); out=bytearray()
for i in range(len(iso)//2048):
    out+=b'\x00'+b'\xff'*10+b'\x00'+bytes(4)+b'\x00\x00\x08\x00'*2+iso[i*2048:(i+1)*2048]+bytes(280)
open(sys.argv[2],'wb').write(out)
PY
rm "$T/game1.iso"
printf 'FILE "Game One.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n' > "$T/discs/Game One.cue"
touch "$T/discs/Game One.jpg"
mkiso "$T/discs/Other.iso" SLUS_000.01
touch "$T/covers/SLUS00001.jpg"
printf 'disc1.cue\ndisc2.cue\n' > "$T/discs/Multi.m3u"
touch "$T/discs/disc1.cue" "$T/discs/disc2.cue" "$T/discs/Homebrew.exe" "$T/discs/sub/Deep.chd" "$T/discs/sub/loose.bin" "$T/discs/readme.txt"
touch "$T/states/Game_One/0" "$T/states/Game_One/3"
FE=$SRC/frontend/riscos
gcc -g -O1 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -fsanitize=address,undefined -I"$FE" \
  -I"$SRC/deps/libchdr/deps/miniz-3.1.2" -DMINIZ_NO_STDIO -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_ARCHIVE_APIS \
  -o "$T/test_library" "$HERE/test_library.c" "$FE/library.c" "$FE/archive.c" "$FE"/lzma/*.c \
  "$SRC/deps/libchdr/deps/miniz-3.1.2/miniz.c"
"$T/test_library" "$T" 2>&1 | grep -v "^Library:"
