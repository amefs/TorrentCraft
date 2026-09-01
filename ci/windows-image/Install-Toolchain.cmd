@echo off
setlocal EnableExtensions

set "VCPKG_COMMIT=d92484ed3c5020c6679d095ad3e5add907887b62"
set "MINGIT_VERSION=2.46.0"
set "VS_INSTALL_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"

mkdir C:\TEMP
curl.exe --fail --location --silent --show-error --output C:\TEMP\vs_buildtools.exe https://aka.ms/vs/17/release/vs_buildtools.exe
if errorlevel 1 exit /b %errorlevel%

start "Visual Studio Build Tools" /w C:\TEMP\vs_buildtools.exe --quiet --wait --norestart --nocache --installPath "%VS_INSTALL_PATH%" --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.26100 --add Microsoft.VisualStudio.Component.Vcpkg
set "install_exit_code=%errorlevel%"
if "%install_exit_code%"=="3010" set "install_exit_code=0"
if not "%install_exit_code%"=="0" exit /b %install_exit_code%

curl.exe --fail --location --silent --show-error --output C:\TEMP\mingit.zip https://github.com/git-for-windows/git/releases/download/v%MINGIT_VERSION%.windows.1/MinGit-%MINGIT_VERSION%-64-bit.zip
if errorlevel 1 exit /b %errorlevel%

powershell.exe -NoLogo -NoProfile -Command "Expand-Archive -LiteralPath C:\TEMP\mingit.zip -DestinationPath C:\Tools\MinGit -Force"
if errorlevel 1 exit /b %errorlevel%

C:\Tools\MinGit\cmd\git.exe clone --filter=blob:none https://github.com/microsoft/vcpkg.git C:\vcpkg
if errorlevel 1 exit /b %errorlevel%

cd /d C:\vcpkg
C:\Tools\MinGit\cmd\git.exe checkout %VCPKG_COMMIT%
if errorlevel 1 exit /b %errorlevel%

call bootstrap-vcpkg.bat -disableMetrics
if errorlevel 1 exit /b %errorlevel%

del /q C:\TEMP\vs_buildtools.exe C:\TEMP\mingit.zip
exit /b 0
