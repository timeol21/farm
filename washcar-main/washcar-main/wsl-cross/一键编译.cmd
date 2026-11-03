@echo off
REM ===========================================================================
REM  washcar one-click build launcher  (double-click me)
REM
REM  This file is intentionally pure ASCII: cmd.exe reads .cmd files with the
REM  console code page, so non-ASCII comments would come out garbled. All
REM  Chinese UI lives in build-menu.ps1 (UTF-8 with BOM).
REM
REM  Usage:
REM    double-click                -> interactive menu
REM    scripted use:                <this file> -Mode aarch64 -Targets main -Yes
REM    environment check only:      <this file> -Check
REM ===========================================================================
setlocal
chcp 65001 >nul 2>&1

set "HERE=%~dp0"

REM Prefer PowerShell 7 (better UTF-8 / progress rendering), fall back to 5.1
set "PSEXE=powershell"
where /q pwsh.exe && set "PSEXE=pwsh"

"%PSEXE%" -NoProfile -ExecutionPolicy Bypass -File "%HERE%build-menu.ps1" %*
set "RC=%ERRORLEVEL%"

REM With args (scripted use) just return the exit code, no interaction.
if not "%~1"=="" exit /b %RC%

REM Errors before the menu (exit code >= 2) would close the window instantly;
REM keep it open so the message can be read.
if %RC% geq 2 pause

exit /b %RC%
