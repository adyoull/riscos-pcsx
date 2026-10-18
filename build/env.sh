# Settings for build.sh and package.sh. Source it.
VERSION=r26-riscos7test
# PCSX ReARMed (github.com/notaz/pcsx_rearmed), master on 2026-10-03
UPSTREAM_URL=https://github.com/notaz/pcsx_rearmed.git
UPSTREAM_COMMIT=8b6c1635
# the submodules the libretro build needs (as pinned by that commit)
SUBMODULES="deps/libchdr deps/libretro-common"
# riscos-crossdev: toolchain 1.4 (GCCSDK GCC 10.2, UnixLib 5.0.3.3)
CROSSDEV_URL=https://github.com/adyoull/riscos-crossdev.git
CROSSDEV_TAG=v1.8.2
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WORK=${WORK:-$HERE/work}
: "${RCD_DIR:=$WORK/riscos-crossdev}"
SRC=$WORK/pcsx_rearmed
OUT=$WORK/build
DIST=$HERE/dist
