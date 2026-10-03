@echo off
rem Run ZacOS 9 in a virtual machine (QEMU in WSL Debian), from the
rem ISO that scripts/build-iso.sh makes. Options, e.g. "vm --install":
rem   --install   also attach a 20 GB disk, to try the installer
rem   --disk      boot the installed disk
rem   --uefi      boot with UEFI firmware
wsl -d Debian --cd "%~dp0" -- scripts/vm.sh %*
if errorlevel 1 pause
