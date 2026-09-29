@echo off
setlocal DisableDelayedExpansion
rem SPDX-License-Identifier: GPL-3.0-only
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
echo Starting Rpi5Display long-run soak recorder...
echo.
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Soak-Watch.ps1" -Mode Start
set "RESULT=%ERRORLEVEL%"
echo.
echo Recorder exited with code: %RESULT%
echo This window is intentionally kept open so any error is visible.
echo Check the local Logs folder for recorder-error-*.txt.
pause
exit /b %RESULT%
