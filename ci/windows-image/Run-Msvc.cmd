@echo off
setlocal

rem Keep container CI output deterministic without altering the host system locale.
chcp 65001 >nul
set "VSLANG=1033"

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64
if errorlevel 1 exit /b %errorlevel%

set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;C:\Tools\MinGit\cmd;C:\vcpkg;%PATH%"
set "VCPKG_ROOT=C:\vcpkg"
set "VCPKG_DISABLE_METRICS=1"

if "%~1"=="" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass
) else (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass %*
)
