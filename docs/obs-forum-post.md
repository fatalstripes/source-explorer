<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# OBS Forums resource: ready-to-paste text

Where: https://obsproject.com/forum/resources/ > "Add resource" (needs your OBS Forums account).
This file is only the text to paste; nothing here has been posted.

## Fields

| Field | Value |
|---|---|
| Title | Source Explorer |
| Tag line | Edit everything about a source in one dock, and copy Streamer.bot / obs-websocket commands for any field |
| Category | Plugins (Tools if there is no better fit) |
| Version | 0.2.1 (work in progress) |
| Supported OBS | 32.2.x, Windows 64-bit |
| License | GPL-2.0-or-later |
| Download | External link: https://github.com/fatalstripes/source-explorer/releases/latest (or the v0.2.1 release page) |
| Source code | https://github.com/fatalstripes/source-explorer |
| Support | https://github.com/fatalstripes/source-explorer/issues |

## Description

**Source Explorer** puts everything about a source in one dock: pick a source (or just click it in OBS) and edit its
transform, audio, filters and properties without opening five windows. Right-click almost any field to copy the
matching **Streamer.bot (C#)** or **obs-websocket v5** command for it, including Get and Reset to Default.

It is an independent, from-scratch implementation inspired by the StreamUP Source Explorer
(https://docs.streamup.tips/obs/streamup_source_explorer.html). It shares no code with it.

**What it does**

- Identity bar with live badges (On Program, In Preview, Muted, filter count), a Scene / Group / Source status bar with a
  warning when the source is in a scene you are not editing, and a quick-access bar that adapts to the source
  (visibility, lock, snapshot, pin, mute, monitoring, browser refresh, capture activate, transform popup, media controls).
- Tabs that hide when they do not apply: Overview (with a welcome card), Source, Transform, Audio, Filters, Properties, Misc.
- **Filters** laid out like OBS's own dialog (Audio/Video and Effect lists, separate tabs for the source, its group and its
  scene): enable, drag or move up/down, search, add (limited to filters the source can take), inline property editors,
  popped-out filter windows, and copy/paste of filters between sources.
- **Select a source** dialog with search and filters by source type, filter type, visible-only, live-only and scenes-only.
  Sources in a scene that is not live load without switching scenes.
- **Detachable tabs**: right-click a tab (or drag it off) to give it its own OBS dock; it is remembered across restarts.
- Source tree drawer with live show/lock toggles, and canvas-aware transform tools (centre, fit, fill, stretch, rotate,
  flip, copy/paste transform).

**Install**

1. Close OBS completely.
2. Run `SourceExplorer-Setup.exe` (or `Install.cmd` from the zip).
3. Start OBS, open the **Docks** menu and tick **Source Explorer**.

Windows SmartScreen may warn because the installer is not code-signed. The installer refuses to run on an OBS version
other than the one the plugin was built for, because loading it into the wrong OBS can crash OBS.

**Status and limits (please read)**

- Work in progress. Built and tested against **OBS Studio 32.2.2 (Qt 6.11) on Windows 64-bit only**.
- Filters copied here cannot be pasted with OBS's own Paste Filters (OBS gives plugins no access to its clipboard).
- obs-websocket has no requests for scale filtering or deinterlacing, so those two fields cannot copy a command.
- Not yet tested on other machines: if the installer misbehaves for you, please report it.

**Reporting problems:** open an issue at https://github.com/fatalstripes/source-explorer/issues and include your OBS
version, what happened, and the OBS log (Help > Log Files > Upload Current Log File).

## Screenshots to attach

Take these in OBS with a source selected (none are included in the repo):

1. The dock on the Transform tab with the Command Copier right-click menu open.
2. The Filters tab with one row expanded.
3. The Select a source dialog.
4. A tab detached into its own dock.
