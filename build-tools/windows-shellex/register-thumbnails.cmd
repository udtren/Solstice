@echo off
rem Shows thumbnails of .kra and .krz files in Explorer for the current user.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0register-thumbnails.ps1" %*
pause
