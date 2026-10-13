@echo off
rem Double-click to remove Source Explorer from OBS Studio.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Uninstall %*
