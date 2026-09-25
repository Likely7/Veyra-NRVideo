@echo off
rem Qt 6.8.3 + D3D12 host probe (S3.0). Its own build directory so the verified
rem product build tree is never reconfigured by Qt. Non-ASCII paths come from the
rem environment: a .cmd is parsed in the ANSI code page.
rem Use scripts/build-qt-probe.ps1.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b 4
set "ROOT=%~dp0.."
if "%VEYRA_QT_OUT%"=="" exit /b 3
set "OUT=%VEYRA_QT_OUT%"
if not "%VEYRA_QT_TMP%"=="" set "TEMP=%VEYRA_QT_TMP%"
if not "%VEYRA_QT_TMP%"=="" set "TMP=%VEYRA_QT_TMP%"
set "CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%OUT%\CMakeCache.txt" (
  "%CMAKE%" --preset x64-release -S "%ROOT%" -B "%OUT%" -DVEYRA_BUILD_QT_PROBE=ON -DCMAKE_PREFIX_PATH="%VEYRA_QT_DIR%" -DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON -DVEYRA_DLSS_SDK_ROOT="%ROOT%\third_party_local\nvidia\DLSS_SDK_310.7.0" -DVEYRA_NVOF_SDK_ROOT="%ROOT%\third_party_local\nvidia\Optical_Flow_SDK_5.0.7" -DVEYRA_NVENC_HEADERS_ROOT="%ROOT%\third_party_local\nvidia\nv-codec-headers-13.0" -DVEYRA_XESS_ROOT="%ROOT%\third_party_local\intel\xess-3.0.2" -DVEYRA_FIDELITYFX_ROOT="%ROOT%\third_party_local\amd\FidelityFX-SDK\sdk" -DVEYRA_FIDELITYFX2_ROOT="%ROOT%\third_party_local\amd\FidelityFX-SDK-2.3.0\Kits\FidelityFX" -DVEYRA_FFMPEG_ROOT="C:\veyra-deps\ffmpeg-ps5-dav1d-installed" -DVEYRA_CLIP_TOOLS_ROOT="C:\veyra-deps\tools-installed\x64-windows"
  if errorlevel 1 exit /b 5
)
if "%~1"=="" (
  "%CMAKE%" --build "%OUT%"
) else (
  "%CMAKE%" --build "%OUT%" --target %*
)
if errorlevel 1 exit /b 6
exit /b 0
