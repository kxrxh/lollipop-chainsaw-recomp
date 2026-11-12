@echo off
setlocal
if exist "%~dp0out\installer\Lollipop Chainsaw Setup.exe" (
  start "" "%~dp0out\installer\Lollipop Chainsaw Setup.exe"
) else (
  echo Build the installer with scripts\package-installer.ps1 first.
  pause
)
