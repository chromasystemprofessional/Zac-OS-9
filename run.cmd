@echo off
rem Build and launch Platinum 2026 in a window (via WSL Debian + WSLg).
rem Double-click this file, or run "run" from a terminal in this folder.
rem The window may open behind other windows: look for
rem "wlroots - WL-1 (Debian)" on the taskbar.
wsl -d Debian --cd "%~dp0" -- scripts/run-nested.sh %*
if errorlevel 1 pause
