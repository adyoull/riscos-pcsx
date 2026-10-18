#!/bin/bash
# Builds !PCSX's program: build/build.sh [fetch|core|frontend|all]
#  fetch     riscos-crossdev (at CROSSDEV_TAG, its prebuilt toolchain) and
#            PCSX ReARMed at UPSTREAM_COMMIT with the patches applied
#  core      the libretro core as a static library
#  frontend  the frontend, linked with the core; the checks
# Needs the toolchain tarball: RCD_DIR may point at an installed
# riscos-crossdev instead (with toolchain/env).
set -euo pipefail
. "$(dirname "$0")/env.sh"
what=${1:-all}

fetch() {
  mkdir -p "$WORK"
  if [ ! -x "$RCD_DIR/toolchain/env/bin/arm-riscos-gnueabihf-gcc" ]; then
    [ -d "$RCD_DIR" ] || git clone -q "$CROSSDEV_URL" "$RCD_DIR"
    git -C "$RCD_DIR" checkout -q "$CROSSDEV_TAG"
    make -C "$RCD_DIR" prebuilt
  fi
  if [ ! -d "$SRC/.git" ]; then
    git clone -q "$UPSTREAM_URL" "$SRC"
  fi
  git -C "$SRC" checkout -q -B riscos "$UPSTREAM_COMMIT"
  git -C "$SRC" submodule update -q --init $SUBMODULES
  for p in $(cat "$HERE/patches/pcsx_rearmed/series"); do
    git -C "$SRC" -c user.name="Andrew Youll" -c user.email=adyoull@users.noreply.github.com \
      am -q --committer-date-is-author-date "$HERE/patches/pcsx_rearmed/$p"
  done
  echo "fetched: $(git -C "$SRC" log --oneline -1)"
}

core() {
  PATH=$RCD_DIR/toolchain/env/bin:$PATH make -C "$SRC" -f Makefile.libretro platform=riscos \
    NEON_ALIGN="$RCD_DIR/checks/neon-align.py" -j"$(nproc)" >"$WORK/core-build.log" 2>&1 \
    || { grep -iE "error" "$WORK/core-build.log" | head -20; exit 1; }
  echo "core: $SRC/pcsx_rearmed_libretro_riscos.a"
}

frontend() {
  mkdir -p "$OUT"
  PATH=$RCD_DIR/toolchain/env/bin:$PATH make -f "$HERE/build/Makefile.frontend" \
    SRC="$SRC" OUT="$OUT" CORE="$SRC/pcsx_rearmed_libretro_riscos.a" VERSION="$VERSION" \
    >"$WORK/frontend-build.log" 2>&1 || { grep -iE "error|undefined" "$WORK/frontend-build.log" | head -20; exit 1; }
  "$RCD_DIR/checks/check-all.sh" "$OUT/pcsx.elf"
}

case $what in
  fetch) fetch ;;
  core) core ;;
  frontend) frontend ;;
  all) fetch; core; frontend ;;
  *) echo "usage: $0 [fetch|core|frontend|all]" >&2; exit 2 ;;
esac
