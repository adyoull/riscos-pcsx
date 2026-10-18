# Changelog

## r26-riscos5test (2026-10-18)
- Games library window (the icon bar icon opens it; Adjust opens the game):
  folders dragged in are scanned (subfolders too; a .cue's .bin and an
  .m3u's discs are one game), played games are added, "Recently played"
  at the top, then all games by name or last played. Each row: cover,
  title, region and serial (read from the disc's SYSTEM.CNF; .chd and
  .pbp when first played), last played, save states, own settings.
  Double-click or Return plays. Menu: Play, Game settings, Remove, Sort,
  Rescan, Folders, Show covers. Kept in <Choices$Write>.PCSX.Library.
- Covers: JPEGs (SpriteExtend's JPEG_PlotScaled) next to the disc, in a
  covers folder beside it, or in <Choices$Write>.PCSX.covers by serial or
  name.
- Per-game settings (high resolution, dynarec, built-in BIOS, full screen,
  pad 1 type) in <Choices$Write>.PCSX.games.<serial>, layered over Choices
  when the game starts.
- The title bar shows the game's name with spaces (not underscores).
- The fps counter starts afresh when a game loads.

## r26-riscos4test (2026-10-18)
- The title bar now shows emulated frames a second (60, or 50 for PAL),
  the game's speed; still screens showed "0 fps" before. The log still
  has both.
- A dropped file that doesn't open (SparkFS, the first time) is tried
  again while the desktop keeps polling, for up to 5 s, instead of in a
  busy loop (which stopped SparkFS fetching it).

## r26-riscos3test (2026-10-18)
- The title bar's frame rate stayed at 0 on the Pi (the log counted ~43):
  the title bar is now also redrawn the classic way (Wimp_ForceRedraw of
  its screen area, from Wimp_GetWindowOutline), and the "Frames:" log
  line shows the title text.
- The high resolution renderer (pcsx_rearmed_neon_enhancement_enable)
  aborted with an alignment fault: without mmap the core's buffers came
  from calloc (8-byte aligned) and the NEON GPU uses :128 alignment hints
  on its enhancement buffer. psxMap now gives page-aligned memory (patch
  0006). The test suite also runs with it on.

## r26-riscos2test (2026-10-18)
After the first Pi log (dynarec, sound and pacing working):
- "Frames:" timings in microseconds (UnixLib's CLOCK_MONOTONIC); they read
  0.0 ms with the centisecond clock. Also counts Wimp polls.
- A dropped file that won't open (SparkFS archives on a network share, the
  first time) is retried for up to a second before loading.
- Core options are logged once, then only when Choices changes one.

## r26-riscos1test (2026-10-18)
First test build.
- PCSX ReARMed master 8b6c1635 (r26-89) as a static libretro core for RISC OS:
  ari64 dynarec (translation cache in .bss), NEON GPU, no threads.
- RISC OS alignment rules: NEON assembler rewritten at build time
  (riscos-crossdev neon-align.py); dynarec get_reg and a gpu_neon word store
  fixed (patch 0002).
- Native frontend: Wimp window and full screen, sprite plotting, SSB sound
  (paced by the sound), keyboard, USBJoystick pads (2 players), save states
  (F2/F4, slots F6/F7), pause F8, full screen F11/Alt-Return, Choices file,
  drag and drop of .cue/.bin, .chd, .pbp, .iso, .m3u, .exe.
- .cue files naming files with spaces find them as hard spaces (patch 0004).
