#!/bin/bash
# Runs every ps1-tests program (JaCzekanski/ps1-tests, MIT) for FRAMES
# frames under the alignment trap, with the dynarec and with the
# interpreter, and compares the pictures.
#   suite.sh HEADLESS TESTS_DIR [FRAMES] [OUTDIR]
set -u
HL=$1; T=$2; N=${3:-300}; O=${4:-/tmp/suite}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$O"; fail=0
printf "%-28s %-6s %-22s %-22s %s\n" test exit drc interp sound
for exe in $(cd "$T" && find . -name '*.exe' ! -path './tools/*' | sort); do
  name=$(basename "$exe" .exe)
  r1=$(TEST_DIR=$O/d-$name "$HERE/aligntrap.sh" "$HL" "$T/$exe" $N "$O/$name-drc.ppm" pcsx_rearmed_drc=enabled 2>"$O/$name-drc.log"); e1=$?
  r2=$(TEST_DIR=$O/i-$name "$HERE/aligntrap.sh" "$HL" "$T/$exe" $N "$O/$name-int.ppm" pcsx_rearmed_drc=disabled 2>"$O/$name-int.log"); e2=$?
  c1=$(echo "$r1" | awk '/^frame/{print $3, $5}'); c2=$(echo "$r2" | awk '/^frame/{print $3, $5}')
  snd=$(echo "$r1" | awk '/^audio/{print $6}')
  st="$e1/$e2"; [ "$e1" = 0 ] && [ "$e2" = 0 ] || { fail=1; st="$st FAULT"; grep -h "test: signal" "$O/$name-drc.log" "$O/$name-int.log" | head -2; }
  printf "%-28s %-6s %-22s %-22s %s %s\n" "$name" "$st" "$c1" "$c2" "$snd" "$([ "$c1" = "$c2" ] && echo same || echo DIFFER)"
done
exit $fail
