#!/bin/bash
# Makes dist/PCSX-$VERSION.zip from build.sh's program: !PCSX with
# !RunImage (stripped, elf2aif -e), !Sprites, !Help, Resources, docs.
set -euo pipefail
. "$(dirname "$0")/env.sh"
ELF=$OUT/pcsx.elf
TC=$RCD_DIR/toolchain/env/bin
APP=$OUT/app/!PCSX
rm -rf "$OUT/app" && mkdir -p "$APP/Resources" "$APP/docs/Patches" "$DIST"

"$TC/arm-riscos-gnueabihf-strip" -o "$OUT/pcsx.stripped" "$ELF"
"$TC/elf2aif" -e "$OUT/pcsx.stripped" "$APP/!RunImage,ff8" >/dev/null
# nothing of the build machine in what's shipped
if strings "$APP/!RunImage,ff8" | grep -iE "claude|/home/|/tmp/cb|/root/" ; then
  echo "package.sh: build paths or names in !RunImage" >&2; exit 1
fi

python3 "$RCD_DIR/package/png2sprite.py" "$APP/!Sprites,ff9" \
  "!pcsx=$HERE/app/pcsx.png" "sm!pcsx=$HERE/app/sm-pcsx.png"
cp "$HERE/app/!Boot,feb" "$HERE/app/!Run,feb" "$APP/"
sed "s/@VERSION@/$VERSION/" "$HERE/app/!Help,fff" > "$APP/!Help,fff"
cp "$HERE/app/Resources/Choices,fff" "$HERE/app/Resources/UserChoices,fff" "$APP/Resources/"

# docs
cp "$SRC/COPYING" "$APP/docs/COPYING,fff"
cp "$SRC/AUTHORS" "$APP/docs/AUTHORS,fff"
cp "$HERE/patches/pcsx_rearmed/"*.patch "$APP/docs/Patches/"
for f in "$APP/docs/Patches/"*.patch; do mv "$f" "${f%.patch},fff"; done
set +o pipefail   # (head closes its pipe early)
{
  echo "Licences of the code in !PCSX besides PCSX ReARMed's own (GPL, see COPYING)"
  echo
  echo "=== libchdr (deps/libchdr) ==="; cat "$SRC/deps/libchdr/LICENSE.txt"; echo
  echo "=== LZMA SDK (deps/libchdr/deps/lzma; frontend/riscos/lzma, 7-Zip 26.04, for .7z files) ==="; cat "$SRC/deps/libchdr/deps/lzma-26.02/LICENSE"; echo
  echo "=== zstd decompressor (deps/libchdr/deps/zstd) ==="
  sed -n '/Copyright (c) Meta/,/^ \*\//p' "$SRC/deps/libchdr/deps/zstd-1.5.7/zstddeclib.c" | head -12; echo
  echo "=== miniz (deps/libchdr/deps/miniz) ==="
  sed -n '/This is free and unencumbered software/,/unlicense.org/p' "$SRC/deps/libchdr/deps/miniz-3.1.2/miniz.h" | head -30; echo
  echo "=== libretro.h and libretro-common (deps/libretro-common) ==="
  sed -n '/Copyright (C) 2010-20[0-9]* The RetroArch team/,/SOFTWARE\./p' "$SRC/deps/libretro-common/include/libretro.h" | sed -n '2,$p' | head -25
} > "$APP/docs/Licences,fff"
set -o pipefail
cat > "$APP/docs/Source,fff" <<EOT
!PCSX $VERSION

PCSX ReARMed: https://github.com/notaz/pcsx_rearmed at commit
$UPSTREAM_COMMIT, with the patches in Patches (git am, in order).
The RISC OS port's repository (build scripts, these patches, tests) is
riscos-pcsx by Andrew Youll; its source is in riscos-pcsx-source.tar.gz
next to this release, or ask the author.

Built with riscos-crossdev $CROSSDEV_TAG (GCCSDK GCC 10.2, UnixLib).
EOT
[ -s "$APP/docs/Licences,fff" ] || { echo "no licences" >&2; exit 1; }

python3 "$RCD_DIR/package/rozip.py" "$DIST/PCSX-$VERSION.zip" -C "$OUT/app" '!PCSX'
ls -la "$DIST/PCSX-$VERSION.zip"
md5sum "$DIST/PCSX-$VERSION.zip" "$APP/!RunImage,ff8"
