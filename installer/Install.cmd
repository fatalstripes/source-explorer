@echo off
rem Double-click to install Source Explorer for OBS Studio.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
