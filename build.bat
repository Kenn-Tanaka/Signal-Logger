@echo off
setlocal
cd /d "%~dp0"

REM Forward arguments to PowerShell build script
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %ERRORLEVEL%
