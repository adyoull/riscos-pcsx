# riscos-pcsx

PCSX ReARMed (the PlayStation emulator, github.com/notaz/pcsx_rearmed) for
RISC OS 5, with a native desktop frontend: `!PCSX`.

- The core is PCSX ReARMed's libretro core, built as a static library with
  GCCSDK (riscos-crossdev): the ari64 dynamic recompiler (32-bit ARM), the
  NEON GPU, single-threaded.
- The frontend (`frontend/riscos` in the patches) is a Wimp task, no SDL:
  icon bar, a window you drag disc images onto, full screen, menus, frames
  plotted from a 32bpp sprite the core draws into, sound through
  SharedSoundBuffer/StreamManager, keyboard (OS_Byte 121 scans), USB game
  pads (Joystick_Read, the PRM's API, with USBJoystick's newer calls on
  top when present), save states, a Choices file.

Licence: GPL-2.0-or-later, as PCSX ReARMed. AI (Anthropic's Claude) has been
used as a coding assistant on this port.

## Layout

| Path | What |
|---|---|
| `patches/pcsx_rearmed/` | `git am` series on upstream (see `build/env.sh` for the commit) |
| `build/` | `build.sh` (fetch, patch, build, checks), `package.sh` (the zip), `Makefile.frontend` |
| `app/` | `!Run`, `!Boot`, `!Help`, `Resources/Choices`, icons |
| `tests/headless/` | the core + `host.c` with a scripted platform, for ARM Linux under QEMU with alignment traps |
| `tests/fake/` | `plat_riscos.c` and `wimp.c` against riscos-crossdev's Fake RISC OS |

See BUILDING.md and CHANGELOG.md.
