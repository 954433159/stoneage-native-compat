@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0local-game.ps1" -Action start
set "stoneExitCode=%ERRORLEVEL%"
if not "%stoneExitCode%"=="0" pause
exit /b %stoneExitCode%
