# Building !PCSX

Needs Linux x86-64 (Ubuntu 24.04), git, make, python3, and riscos-crossdev
(fetched by build.sh at the tag in `build/env.sh`; or set `RCD_DIR` to an
installed one with its prebuilt toolchain).

    build/build.sh            # fetch + patch upstream, build core and frontend, run the checks
    build/package.sh          # dist/PCSX-<version>.zip

`WORK=dir` puts the trees elsewhere (default `work/`).

## Changing the patches

Work in `work/pcsx_rearmed` (branch `riscos`), commit, then

    git -C work/pcsx_rearmed format-patch -o patches/pcsx_rearmed <upstream>..riscos

and update `patches/pcsx_rearmed/series` if files were added.

## Tests

Need `gcc-arm-linux-gnueabihf`, `qemu-user` and riscos-crossdev's
`qemu-arm-aligntrap` (`qemu/build.sh` there).

    tests/headless/run-build.sh work/pcsx_rearmed /tmp/hl        # core + headless for ARM Linux
    tests/headless/suite.sh /tmp/hl/headless <ps1-tests dir>     # every test program, dynarec vs interpreter
    tests/fake/run.sh work/pcsx_rearmed <riscos-crossdev>        # plat_riscos.c and wimp.c

The ps1-tests programs are JaCzekanski/ps1-tests (MIT), release build-98
(`tests.zip`). `headless` takes key=value settings (Choices names) and
scripted input: `press=FRAME:KEY:LEN`, `joy=`, `hat=`, `stick=`,
`savestate=FRAME:SLOT`, `loadstate=`, `shot=FRAME:FILE.ppm`, `padlog=1`.
