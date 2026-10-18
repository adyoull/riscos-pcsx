#!/bin/bash
# Like riscos-crossdev's qemu/aligntrap.sh, but alignment is checked for
# the whole program image, .bss included: the dynarec's translation cache
# is in .bss, and the code it writes must obey RISC OS's rules too. Only
# ld.so and the shared C library (mapped far above) are exempt.
set -u
prog=$1; shift
end=$(arm-linux-gnueabihf-readelf -lW "$prog" | awk '$1=="LOAD" {e=strtonum($3)+strtonum($6); if (e>m) m=e} END {printf "%x", m}')
lo=$(arm-linux-gnueabihf-readelf -lW "$prog" | awk '$1=="LOAD" {print $3; exit}')
export QEMU_ARM_ALIGN_TRAP=1
export QEMU_ARM_ALIGN_IGNORE=$(printf '0-%x,%x-ffffffff' $((lo)) $((0x$end)))
export QEMU_LD_PREFIX=${QEMU_LD_PREFIX:-/usr/arm-linux-gnueabihf}
exec "${QEMU:-qemu-arm-aligntrap}" -cpu "${QEMU_CPU:-cortex-a8}" ${QEMU_ARGS:-} "$prog" "$@"
