@echo off
rem Build for the QML UI migration branch with the 1.4.4 release configuration.
rem The caller sets VEYRA_OUT (build dir) and VEYRA_TMP (process TEMP): a .cmd file
rem is parsed in the ANSI code page, so non-ASCII paths must come from the environment.
rem Use scripts/build-ui-migration.ps1. Usage: build-ui-migration.cmd [target...]
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b 4
set "ROOT=%~dp0.."
if "%VEYRA_OUT%"=="" exit /b 3
set "OUT=%VEYRA_OUT%"
if not "%VEYRA_TMP%"=="" set "TEMP=%VEYRA_TMP%"
if not "%VEYRA_TMP%"=="" set "TMP=%VEYRA_TMP%"
set "CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%OUT%\CMakeCache.txt" (
  "%CMAKE%" --preset x64-release -S "%ROOT%" -B "%OUT%" -DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON -DVEYRA_DLSS_SDK_ROOT="%ROOT%\third_party_local\nvidia\DLSS_SDK_310.7.0" -DVEYRA_NVOF_SDK_ROOT="%ROOT%\third_party_local\nvidia\Optical_Flow_SDK_5.0.7" -DVEYRA_NVENC_HEADERS_ROOT="%ROOT%\third_party_local\nvidia\nv-codec-headers-13.0" -DVEYRA_XESS_ROOT="%ROOT%\third_party_local\intel\xess-3.0.2" -DVEYRA_FIDELITYFX_ROOT="%ROOT%\third_party_local\amd\FidelityFX-SDK\sdk" -DVEYRA_FIDELITYFX2_ROOT="%ROOT%\third_party_local\amd\FidelityFX-SDK-2.3.0\Kits\FidelityFX" -DVEYRA_FFMPEG_ROOT="C:\veyra-deps\ffmpeg-ps5-dav1d-installed" -DVEYRA_CLIP_TOOLS_ROOT="C:\veyra-deps\tools-installed\x64-windows" -DVEYRA_ENABLE_REMOTEPLAY=ON -DVEYRA_RP_CHIAKI_VERIFY_DIR="C:/veyra-deps/chiaki-source" -DVEYRA_RP_CHIAKI_SOURCE_DIR="%ROOT%\out\remoteplay\chiaki-msvc-stage" -DCMAKE_PREFIX_PATH="C:/veyra-deps/remoteplay-installed/x64-windows-static" -DPROTOC="C:/veyra-deps/remoteplay-installed/x64-windows/tools/protobuf/protoc.exe" -DPKG_CONFIG_EXECUTABLE="C:/veyra-deps/vcpkg/downloads/tools/msys2/3e71d1f8e22ab23f/mingw64/bin/pkg-config.exe"
  if errorlevel 1 exit /b 5
)
if "%~1"=="" (
  "%CMAKE%" --build "%OUT%"
) else (
  "%CMAKE%" --build "%OUT%" --target %*
)
if errorlevel 1 exit /b 6
exit /b 0
