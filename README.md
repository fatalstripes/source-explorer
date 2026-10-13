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
- **Tabs** that hide when they do not apply (Overview doubles as a welcome card with a Select a source button while nothing is loaded): Overview, Source, Transform, Audio, Filters, Properties, Misc.
- **Source tree drawer**: scenes, groups and items with live show/lock toggles, search, and the global audio devices. Expand all, collapse all and refresh, and the loaded source in green.
- **Command copier**: right-click a field for its command, or a label for combined commands (position X+Y,
  size, full transform, full payload...). Each offers Streamer.bot C#, obs-websocket Set and Get, plus Reset.
- **Filters tab** laid out like OBS's own dialog (Audio/Video and Effect lists, tabs for the source, its group
  and its scene): enable checkbox, drag or Move up/down, search by name or type, Add menu limited to filters
  the source can take, **inline property editors** (expand a row), **pop out** any filter into its own window
  (a quick-access button closes them all), copy/paste filters between sources (kept across restarts), and
  expanded rows and the last tab remembered.
- **Detachable tabs**: right-click a tab in the rail and choose Detach to give it its own OBS dock (float it, put it on another monitor). The panel keeps following the selection, a Reattach button or Reset UI puts it back, and detached tabs are restored on the next start.
- **Select a source dialog** (click the source name in the dock): the whole scene, group and source tree with name search, filters by source type and by filter type, visible-only, live-only and scenes-only toggles, expand/collapse, and the loaded source in green. OK loads a scene or global device, or selects the scene item in OBS.
- Canvas-aware transform maths (any canvas shape, rotation, flips, crop, bounding boxes) with unit tests.

## Status

Done: everything above, plus copy/paste transform, the Source and Misc tabs, and Pin.

Not done yet (compared with the StreamUP docs):

- Filters: sharing OBS's own filter clipboard (it lives inside OBS's main window and has no public API, so
  this plugin keeps its own).
- Dragging a tab out to detach it (right-click > Detach works).
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

Copyright (C) 2026 the Source Explorer authors. Licensed under the **GNU General Public License, version 2 or
(at your option) any later version** (SPDX: `GPL-2.0-or-later`); see [LICENSE](LICENSE).

The plugin links against OBS Studio (GPL-2.0-or-later) and Qt (LGPL-3.0). "Or later" is what makes the
combination with Qt's LGPL-3.0 allowable, so please keep it. Anyone you give a binary to is entitled to the
matching source, which is this repository.
