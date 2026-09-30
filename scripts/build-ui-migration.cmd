@echo off
rem Target-explicit build wrapper. Use scripts/build-ui-migration.ps1.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b 4
set "ROOT=%~dp0.."
if "%VEYRA_OUT%"=="" exit /b 3
if /I "%VEYRA_UI_TARGET%"=="qml" (
  set "QML_SWITCH=ON"
) else (
  echo VEYRA_UI_TARGET must be qml for the active UI migration 1>&2
  exit /b 7
)
set "OUT=%VEYRA_OUT%"
if not "%VEYRA_TMP%"=="" set "TEMP=%VEYRA_TMP%"
if not "%VEYRA_TMP%"=="" set "TMP=%VEYRA_TMP%"
set "CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%OUT%\CMakeCache.txt" (
  "%CMAKE%" --preset x64-release -S "%ROOT%" -B "%OUT%" -DVEYRA_BUILD_QML_UI=ON -DCMAKE_PREFIX_PATH="%VEYRA_QT_DIR%;C:/veyra-deps/remoteplay-installed/x64-windows-static" -DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON -DVEYRA_DLSS_SDK_ROOT="%ROOT%\third_party_local\nvidia\DLSS_SDK_310.7.0" -DVEYRA_NVOF_SDK_ROOT="%ROOT%\third_party_local\nvidia\Optical_Flow_SDK_5.0.7" -DVEYRA_NVENC_HEADERS_ROOT="%ROOT%\third_party_local\nvidia\nv-codec-headers-13.0" -DVEYRA_XESS_ROOT="%ROOT%\third_party_local\intel\xess-3.0.2" -DVEYRA_FIDELITYFX_ROOT="%ROOT%\third_party_local\amd\FidelityFX-SDK\sdk" -DVEYRA_FIDELITYFX2_ROOT="%ROOT%\third_party_local\amd\FidelityFX-SDK-2.3.0\Kits\FidelityFX" -DVEYRA_FFMPEG_ROOT="C:\veyra-deps\ffmpeg-ps5-dav1d-installed" -DVEYRA_CLIP_TOOLS_ROOT="C:\veyra-deps\tools-installed\x64-windows" -DVEYRA_ENABLE_REMOTEPLAY=ON -DVEYRA_RP_CHIAKI_VERIFY_DIR="C:/veyra-deps/chiaki-source" -DVEYRA_RP_CHIAKI_SOURCE_DIR="%ROOT%\out\remoteplay\chiaki-msvc-stage" -DPROTOC="C:/veyra-deps/remoteplay-installed/x64-windows/tools/protobuf/protoc.exe" -DPKG_CONFIG_EXECUTABLE="C:/veyra-deps/vcpkg/downloads/tools/msys2/3e71d1f8e22ab23f/mingw64/bin/pkg-config.exe"
  if errorlevel 1 exit /b 5
) else (
  findstr /R /C:"^VEYRA_BUILD_QML_UI:BOOL=%QML_SWITCH%$" "%OUT%\CMakeCache.txt" >nul
  if errorlevel 1 (
    echo Existing CMakeCache.txt does not match target %VEYRA_UI_TARGET% 1>&2
    exit /b 8
  )
)
set "PRIMARY_TARGETS=veyra_qml_ui veyra_qml_data_tests veyra_qml_easing_tests veyra_qml_quick_tests"
rem The UI contract always builds all four targets. Do not forward arbitrary
rem command-line targets/options into a backend build from this UI wrapper.
"%CMAKE%" --build "%OUT%" --target %PRIMARY_TARGETS%
if errorlevel 1 exit /b 6
for %%T in (veyra_qml_ui veyra_qml_data_tests veyra_qml_easing_tests veyra_qml_quick_tests) do (
  if not exist "%OUT%\%%T.exe" exit /b 9
)
exit /b 0
