@echo off
setlocal DisableDelayedExpansion
rem SPDX-License-Identifier: GPL-3.0-only
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Soak-Watch.ps1" -Mode Start
exit /b %ERRORLEVEL%
