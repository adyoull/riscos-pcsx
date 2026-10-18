#!/bin/bash
# Makes zip and 7z files of a made-up game and runs test_archive (Linux,
# host gcc): tests/archive/run.sh SRC [7ZR]
#   7ZR: 7-Zip's 7zr (built from github.com/ip7z/7zip, CPP/7zip/Bundles/
#   Alone7z); without it the 7z checks are skipped.
set -euo pipefail
SRC=$1; Z7=${2:-$(command -v 7zr || true)}; HERE=$(cd "$(dirname "$0")" && pwd); T=${T:-/tmp/arctest}
rm -rf "$T" && mkdir -p "$T/src/extra" "$T/arc"
G="Game One & Two (Europe)"
d=$(mktemp -d)
printf 'BOOT = cdrom:\\SLES_123.45;1\r\nTCB = 4\r\n' > "$d/SYSTEM.CNF"
printf 'x' > "$d/SLES_123.45"
head -c 3000000 /dev/urandom > "$d/DATA.BIN"         # bulk that doesn't pack
genisoimage -quiet -V TEST -o "$T/g.iso" "$d"; rm -rf "$d"
python3 - "$T/g.iso" "$T/src/$G.bin" <<'PY'
import sys
iso=open(sys.argv[1],'rb').read(); out=bytearray()
for i in range(len(iso)//2048):
    out+=b'\x00'+b'\xff'*10+b'\x00'+bytes(4)+b'\x00\x00\x08\x00'*2+iso[i*2048:(i+1)*2048]+bytes(280)
out+=bytes(6<<20)                                         # bulk that does
open(sys.argv[2],'wb').write(out)
PY
rm "$T/g.iso"
printf 'FILE "%s.bin" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n' "$G" > "$T/src/$G.cue"
echo notes > "$T/src/readme.txt"; echo a > "$T/src/extra/a.txt"; : > "$T/src/empty.dat"
( cd "$T/src" && python3 - "$T/arc" <<'PY'
import sys, zipfile, os
o=sys.argv[1]
files=[os.path.join(dp,f)[2:] for dp,_,fs in os.walk('.') for f in fs]
for name,meth in (('deflate',zipfile.ZIP_DEFLATED),('stored',zipfile.ZIP_STORED),('lzma',zipfile.ZIP_LZMA)):
    with zipfile.ZipFile(f'{o}/{name}.zip','w',meth) as z:
        for f in files: z.write(f)
with zipfile.ZipFile(f'{o}/zip64.zip','w',zipfile.ZIP_DEFLATED) as z:
    for f in files:
        with open(f,'rb') as i, z.open(f,'w',force_zip64=True) as w: w.write(i.read())
with zipfile.ZipFile(f'{o}/nogame.zip','w') as z: z.write('readme.txt')
# a CloneCD rip, named like a real one
c='Tomba! [U] [SCUS-94236]'
with zipfile.ZipFile(f'{o}/clonecd.zip','w',zipfile.ZIP_DEFLATED) as z:
    z.writestr(c+'.ccd','[CloneCD]\nVersion=3\n')
    z.write('Game One & Two (Europe).bin', c+'.img')
    z.writestr(c+'.sub',bytes(96*100))
with zipfile.ZipFile(f'{o}/multi.zip','w',zipfile.ZIP_DEFLATED) as z:
    z.writestr('Disc 2.cue','FILE "Disc 2.bin" BINARY\n'); z.writestr('Disc 2.bin',b'2'*5000)
    z.writestr('Disc 1.cue','FILE "Disc 1.bin" BINARY\n'); z.writestr('Disc 1.bin',b'1'*5000)
    z.writestr('Game.m3u','Disc 1.cue\nDisc 2.cue\n')
# damaged: a byte of the .bin's data changed (stored, so it still decodes)
b=bytearray(open(f'{o}/stored.zip','rb').read()); i=b.find(b'CD001')+100; b[i]^=0xFF
open(f'{o}/damaged.zip','wb').write(b)
PY
)
if [ -n "$Z7" ]; then
  ( cd "$T/src" && "$Z7" a -bd -bso0 "$T/arc/lzma2.7z" . \
    && "$Z7" a -bd -bso0 -m0=LZMA "$T/arc/lzma.7z" . \
    && "$Z7" a -bd -bso0 -mx0 "$T/arc/copy.7z" . \
    && "$Z7" a -bd -bso0 -ms=off "$T/arc/nonsolid.7z" . \
    && "$Z7" a -bd -bso0 -mx9 "$T/arc/ultra.7z" . \
    && "$Z7" a -bd -bso0 -mf=BCJ "$T/arc/bcj-big.7z" . \
    && "$Z7" a -bd -bso0 -mf=BCJ "$T/arc/bcj-small.7z" readme.txt "$G.cue" )
  head -c $(( $(stat -c%s "$T/arc/lzma2.7z") - 5000 )) "$T/arc/lzma2.7z" > "$T/arc/truncated.7z"
fi
FE=$SRC/frontend/riscos; MZ=$SRC/deps/libchdr/deps/miniz-3.1.2
gcc -g -O1 -Wall -Wno-unused-parameter -Wno-format-truncation -fsanitize=address,undefined -fno-sanitize=alignment \
  -DMINIZ_NO_STDIO -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_ARCHIVE_APIS -I"$FE" -I"$MZ" \
  -o "$T/test_archive" "$HERE/test_archive.c" "$FE/archive.c" "$FE/library.c" "$MZ/miniz.c" "$FE"/lzma/*.c
"$T/test_archive" "$T" "${Z7:+7z}" 2>&1 | grep -v "^Library:"
# and as ARM code with no unaligned access (RISC OS's rule), under QEMU
# trapping unaligned loads and stores
if command -v arm-linux-gnueabihf-gcc >/dev/null && command -v qemu-arm-aligntrap >/dev/null; then
  arm-linux-gnueabihf-gcc -O2 -g -marm -mfpu=neon -mfloat-abi=hard -mno-unaligned-access -Wall \
    -Wno-unused-parameter -Wno-format-truncation -no-pie \
    -DMINIZ_NO_STDIO -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_ARCHIVE_APIS -I"$FE" -I"$MZ" \
    -o "$T/test_archive_arm" "$HERE/test_archive.c" "$FE/archive.c" "$FE/library.c" "$MZ/miniz.c" "$FE"/lzma/*.c
  echo "ARM, unaligned access trapped:" >&2
  "$HERE/../headless/aligntrap.sh" "$T/test_archive_arm" "$T" "${Z7:+7z}" 2>&1 | grep -v "^Library:"
fi
