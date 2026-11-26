@echo off
chcp 65001 >nul
set "SCRIPT=%~dp0versions\2.2.1\runtime\amd-nr-rdna3\mochi\tools\setup-models.ps1"
if not exist "%SCRIPT%" (
    echo [ERROR] Missing script: %SCRIPT%
    pause
    exit /b 1
)
if "%~1"=="" (
    powershell -NoProfile -ExecutionPolicy Bypass -STA -File "%SCRIPT%"
    exit /b %ERRORLEVEL%
)
if exist "%~1" (
    powershell -NoProfile -ExecutionPolicy Bypass -STA -File "%SCRIPT%" -Dll "%~1" %2 %3 %4 %5
    exit /b %ERRORLEVEL%
)
powershell -NoProfile -ExecutionPolicy Bypass -STA -File "%SCRIPT%" %*
exit /b %ERRORLEVEL%
