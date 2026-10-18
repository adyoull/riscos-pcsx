#!/bin/bash
# All the Linux tests: tests/run-all.sh SRC RCD_DIR PS1_TESTS_DIR
set -euo pipefail
SRC=$1; RCD=$2; T=$3; HERE=$(cd "$(dirname "$0")" && pwd)
export PATH=$RCD/toolchain/env/bin:$PATH
"$HERE/fake/run.sh" "$SRC" "$RCD"
NEON_ALIGN=$RCD/checks/neon-align.py "$HERE/headless/run-build.sh" "$SRC" /tmp/pcsx-hl
"$HERE/headless/suite.sh" /tmp/pcsx-hl/headless "$T" 300 /tmp/pcsx-suite
