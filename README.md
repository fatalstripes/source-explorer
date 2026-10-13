# Source Explorer for OBS Studio

A dock for OBS Studio that puts everything about a source in one place: pick a source and edit its
transform, audio, filters and properties without opening five windows. Right-click almost any field
to copy the matching **Streamer.bot (C#)** or **obs-websocket v5** command for it.

It is an independent, from-scratch implementation inspired by the StreamUP Source Explorer
([docs](https://docs.streamup.tips/obs/streamup_source_explorer.html)). It shares no code with it.

> **Status: work in progress.** Built and tested against **OBS Studio 32.2.2 / Qt 6.11 on Windows x64**
> only. See [what is and isn't done](#status).

## Features

- **Dock** with an identity bar (icon, name, type, Live / Preview / Muted / filter-count badges), a
  Scene > Group > Source status bar with a wrong-scene warning, and a quick-access bar that adapts to the
  source (visibility, lock, snapshot, pin, mute, monitoring, browser refresh, capture activate,
  transform popup, media controls).
- **Tabs** that hide when they do not apply: Overview, Source, Transform, Audio, Filters, Properties, Misc.
- **Source tree drawer**: scenes, groups and items with live show/lock toggles, search, and the global audio devices.
- **Command copier**: right-click a field for its command, or a label for combined commands (position X+Y,
  size, full transform, full payload...). Each offers Streamer.bot C#, obs-websocket Set and Get, plus Reset.
- Canvas-aware transform maths (any canvas shape, rotation, flips, crop, bounding boxes) with unit tests.

## Status

Done: everything above, plus copy/paste transform, the Source and Misc tabs, Pin, and filter reorder by drag.

Not done yet (compared with the StreamUP docs):

- Filters tab: Audio/Video vs Effect sections, copy/paste of filters (and OBS's filter clipboard), search,
  expand/collapse, inline property editors per filter, popped-out filter windows.
- Detachable tabs.
- Source picker dialog and the tree's type / filter / visibility filters.
- obs-websocket has no requests for scale filtering or deinterlacing, so those two fields cannot copy a command.

## Building (Windows)

Needs Visual Studio 2022 (C++ workload) and an installed OBS Studio 32.2.x.

```powershell
./cmake/setup-sdk.ps1                                # downloads OBS headers + Qt (hash-checked), makes import libs, configures
cmake --build build --config Release                 # plugin DLL: build/Release/source-explorer.dll
ctest --test-dir build -C Release                    # unit tests + a headless start-up smoke test
```

`setup-sdk.ps1` pins the OBS and obs-deps versions in one place. The plugin must be built against the
same OBS and Qt it runs in; the installer refuses any other combination.

## Layout

| Path | What |
|---|---|
| `src/core/` | Qt-free logic (payload generation, transform maths, tree model, quick-access rules) and libobs glue |
| `src/ui/` | The dock, panels, widgets |
| `tests/` | Unit tests for the pure code, and `smoke_dock`, which builds the real dock against real libobs headlessly |
| `installer/` | `install.ps1`, and `build-installer.ps1` which makes a single-file installer with Windows' IExpress |
| `cmake/` | SDK setup and import-library scripts |

## Installing

Build the installer with `./installer/build-installer.ps1`, or install by hand: copy `source-explorer.dll` to
`%ProgramData%\obs-studio\plugins\source-explorer\bin\64bit\` with OBS closed, then enable the dock from
OBS's **Docks** menu.

## License

*Not yet chosen.* The plugin links against OBS Studio (GPL-2.0-or-later) and Qt (LGPL-3.0), so a binary
release has to be offered with matching source under compatible terms.
