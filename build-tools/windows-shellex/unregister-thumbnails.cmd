@echo off
rem Removes the thumbnail registration made by register-thumbnails.cmd.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0register-thumbnails.ps1" -Unregister %*
pause
