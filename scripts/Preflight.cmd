@echo off
setlocal DisableDelayedExpansion
rem SPDX-License-Identifier: GPL-3.0-only
set "ACTION=Preflight"
if /i "%~1"=="/?" set "ACTION=Help"
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%~dp0Run-Lab.ps1" (
  echo Extract the complete installer ZIP before running this file.
  exit /b 2
)
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-Lab.ps1" -Action %ACTION%
set "RESULT=%ERRORLEVEL%"
echo.
echo Rpi5Display launcher exit code: %RESULT%
if /i not "%ACTION%"=="Help" pause
exit /b %RESULT%
