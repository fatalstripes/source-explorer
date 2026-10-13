SOURCE EXPLORER FOR OBS STUDIO
==============================

One dock that replaces a lot of OBS menus: pick a source and edit its transform, audio, filters
and properties in one place. Right-click almost anything to copy the matching Streamer.bot (C#)
or obs-websocket command.

This is a work-in-progress build. Expect rough edges, and please report what you find.


WHAT YOU NEED
-------------
  * Windows 10/11, 64-bit
  * OBS Studio 32.2.x, the normal Windows installer version
    (not the Steam or portable version)

The plugin is built for one exact OBS version. The installer checks this and will refuse to
install on a different one, because loading it into the wrong OBS can crash OBS on start-up.


INSTALL
-------
  1. Close OBS completely. File > Exit, and check the system tray near the clock.
  2. Double-click  SourceExplorer-Setup.exe  (or  Install.cmd  if you got the zip).
  3. If Windows SmartScreen says "Windows protected your PC", click "More info", then
     "Run anyway". It appears because the installer is not code-signed.
  4. Start OBS, open the  Docks  menu and tick  Source Explorer.

Tip: drag the dock's title bar onto another dock's tab strip to make it a tab, like Chat.


UNINSTALL
---------
  * Close OBS, then double-click  Uninstall.cmd  (zip version), or
  * delete this folder:  C:\ProgramData\obs-studio\plugins\source-explorer


IF SOMETHING GOES WRONG
-----------------------
  * OBS shows "crash detected" after a bad start: choose "Run in Normal Mode".
  * OBS crashes on start-up with the plugin installed: uninstall it (above) and send the newest
    file in  %APPDATA%\obs-studio\crashes  to the author.
  * The dock is missing: Docks menu > Source Explorer. If it is not listed, check
    %APPDATA%\obs-studio\logs  for "source-explorer".

The plugin never changes your scenes or settings by itself; it only acts when you click.


LICENSE AND SOURCE CODE
-----------------------
Source Explorer is free software, licensed under the GNU General Public License version 2 or (at your
option) any later version. It links against OBS Studio (GPL-2.0-or-later) and Qt (LGPL-3.0).
You are entitled to the source code of this build: ask the author, or see the project's GitHub page.
