# Полное руководство по зависимостям, окружению, переводу и сборке Veyra (Veyra-NRVideo)

Данный документ содержит **исчерпывающую информацию** обо всех внешних зависимостях, SDK, системных утилитах, библиотеках, правилах перевода и шагах сборки проекта **Veyra** с поддержкой **NVIDIA DLSS, DLSS-NR, Frame Generation, Intel XeSS, AMD FidelityFX и PS5 RemotePlay**.

---

## 1. Что должно быть установлено в Windows (Системный софт и утилиты)

### 1.1. Компилятор и SDK
* **Visual Studio 2022** (или **Visual Studio Build Tools 2022**):
  * Версия: **17.14** (MSVC Toolset **v143**, компилятор `cl.exe` версии `19.44.35207`).
  * Обязательные компоненты в VS Installer:
    * *Разработка классических приложений на C++ (Desktop development with C++)*
    * *MSVC v143 - VS 2022 C++ x64/x86 build tools*
    * *Windows 11 SDK (10.0.22621.0 или 10.0.26100.0)*
  * *Важно:* Если на ПК установлена превью-версия Visual Studio 2025 (VS 18), убедитесь, что скрипты используют именно VS 2022 (`[17, 18)`), так как toolchain v144 ломает обратную совместимость некоторых бинарных зависимостей.

### 1.2. Системные утилиты сборки
* **CMake**:
  * Версия: `3.28+` (установлен `4.4.3` в `C:\Program Files\CMake\bin\cmake.exe`).
  * Должен быть добавлен в системный `PATH`.
* **Ninja**:
  * Быстрый генератор сборки. Исполняемый файл `ninja.exe` помещён в `C:\tools\ninja.exe` (или в `PATH`).
* **Git**:
  * Git для Windows с поддержкой длинных путей (`git config --system core.longpaths true`).
* **Python**:
  * Python 3.10 – 3.14 (64-bit).
  * Установленные pip-пакеты (обязательны для генератора nanopb и protobuf в RemotePlay):
    ```powershell
    pip install protobuf grpcio-tools
    ```

---

## 2. Загружаемые SDK от вендоров (Каталог `C:\tools`)

Для сборки всех графических бэкендов в `C:\tools` должны быть распакованы официальные SDK:

| SDK / Технология | Точная версия | Каталог на диске | Что внутри проверяется сборщиком |
|---|---|---|---|
| **NVIDIA Optical Flow SDK** | 5.0.7 | `C:\tools\Optical_Flow_SDK_5.0.7` | `NvOFInterface/nvOpticalFlowCommon.h` |
| **Intel XeSS SDK** | 3.0.2 | `C:\tools\XeSS_302` | `inc/xess/xess.h`, `bin/libxess.dll`, `bin/libxess_fg.dll` |
| **AMD FidelityFX SDK** | 1.1.4 | `C:\tools\FFX_SDK-114\sdk` | `include/FidelityFX/host/ffx_interface.h` |
| **NVIDIA Video Codec SDK** | 13.1 / 13.0 | `C:\tools\Video_Codec_SDK_13.1.15` | `Interface/nvEncodeAPI.h` (или `nv-codec-headers 13.0`) |
| **NVIDIA DLSS SDK** | 310.7.0 | Располагается внутри репозитория: `third_party_local/nvidia/DLSS_SDK_310.7.0` | `include/nvsdk_ngx.h`, `lib/Windows_x86_64/x64/nvsdk_ngx_s.lib` |

---

## 3. Специально подготовленные зависимости (`C:\veyra-deps`)

Veyra предъявляет жесткие требования к CRT (компиляция со статическим рантаймом `/MT`). Из-за этого стандартные библиотеки из интернета вызывают ошибки компоновщика. В `C:\veyra-deps` подготовлены:

### 3.1. Патченный FFmpeg 9.0.1 (PS5 Slice Patch + dav1d)
* Путь: `C:\veyra-deps\ffmpeg-ps5-dav1d-installed`
* Особенности:
  * Включает патч расширения емкости H.264 срезов (32 -> 256 слайсов) для декодирования видеопотока с PlayStation 5 без артефактов и дропов.
  * Скомпилирован с поддержкой AV1 через библиотеку `dav1d.dll`.
  * Содержит заголовочные файлы (`include/libav*`), файлы библиотек импорта (`lib/av*.lib`) и рантайм DLL (`bin/av*.dll`, `bin/dav1d.dll`).

### 3.2. AMD FidelityFX SDK скомпилированный под `/MT`
* Путь: `C:\veyra-deps\FFX_SDK-114-MT\sdk`
* Зачем: Официальный бинарник AMD собран с динамическим CRT (`/MD`), а Veyra и статический шим NVIDIA NGX требуют статический CRT (`/MT`).
* Библиотеки в `C:\veyra-deps\FFX_SDK-114-MT\sdk\bin\ffx_sdk\`:
  * `ffx_backend_dx12_x64.lib`, `ffx_opticalflow_x64.lib`, `ffx_fsr3upscaler_x64.lib`, `ffx_frameinterpolation_x64.lib` (все собраны с `/MT`).

### 3.3. Исходный код Chiaki для RemotePlay
* Чистый клон (Clean Checkout): `C:\veyra-deps\chiaki-clean` (коммит `0e16950165f06e5c3291537c2eeba6e852be7120` со всеми 8 подмодулями).
* Патченный стейдж (Staged Worktree): `C:\veyra-deps\chiaki-stage`
  * Наложены патчи из репозитория Veyra:
    1. `scripts/remoteplay/patches/0001-chiaki-msvc-vla-compat.patch`
    2. `scripts/remoteplay/patches/0002-chiaki-video-metadata.patch`

### 3.4. Пакетный менеджер `vcpkg` (для статических библиотек)
Установлен в `C:\vcpkg`. Все библиотеки собраны под триплет **`x64-windows-static`**:
* `json-c:x64-windows-static`
* `libevent:x64-windows-static` (с поддержкой потоков)
* `miniupnpc:x64-windows-static`
* `openssl:x64-windows-static`
* `opus:x64-windows-static`
* `protobuf:x64-windows-static` (компилятор протоколов `protoc.exe` в `C:\vcpkg\installed\x64-windows\tools\protobuf\protoc.exe`)
* `sdl3:x64-windows-static` (версия 3.4.16+)
* Утилита `pkgconf.exe` (из дистрибутива msys2 в каталоге vcpkg: `C:\vcpkg\downloads\tools\msys2\...\mingw64\bin\pkgconf.exe`).

---

## 4. Локализация и перевод (Перевод новых версий)

Оригинальный репозиторий разрабатывается на китайском языке. Тексты зашиты непосредственно в код интерфейса C++.

### Список файлов для перевода:
* **Интерфейс приложения (`apps/veyra/ui/`)**:
  * `AppShell.cpp` — главное окно, заголовок, меню выбора источников, вкладки.
  * `CapturePanel.cpp` — настройки карты захвата (Elgato, AverMedia, разрешение, цветовой диапазон).
  * `ScreenCapturePanel.cpp` — параметры захвата экрана и окон.
  * `RemotePlayPanel.cpp` — подключение к PS5, регистрация консоли, битрейт, пин-коды.
  * `SettingHelp.h` — детальные подсказки при наведении на переключатели DLSS, FSR, генерации кадров, моделей шума.
  * `SubtitleSettingsPanel.h` — шрифты, цвета, стили и дорожки субтитров.
  * `LiveStatusDashboard.h` и `LiveStatusPanel.h` — плашки телеметрии, FPS, времени задержки (Frame Pacing, GPU/Display Submit).
* **Окна настроек (`apps/veyra/`)**:
  * `SettingsWindow.cpp` — глобальные настройки приложения, видеовыходы, горячие клавиши.
  * `TelemetryWindow.cpp` — расширенный лог производительности в реальном времени.
* **Встроенные профили (`src/engine/`)**:
  * `PresetStore.cpp` — предустановленные пресеты обработки видео.
  * `VideoExportJob.cpp` — уведомления о ходе и завершении экспорта файла.

### Правила при переводе:
1. Сохраняйте кодировку: строго **UTF-8 (без BOM)**.
2. Не изменяйте имена JSON/INI ключей (например, `"ngxProjectId"`, `"engineVersion"`), системные переменные окружения и теги логирования (внутри квадратных скобок: `[INFO ] [graph]`).
3. При обновлении от автора используйте команду:
   ```powershell
   git diff <старый_коммит> <новый_коммит> -- apps/veyra/ui/
   ```
   чтобы перевести только свежедобавленные фразы.

---

## 5. Обязательные исправления в исходниках (Патчи под компилятор MSVC)

Если вы скачали свежее обновление от автора, в коде могут повторяться ошибки, ломающие сборку в MSVC 2022:

1. **`src/pipeline/EnhanceGraph.cpp` (Ошибка C1001 / падение компилятора `CloseTypeServerPDB`)**:
   * **Проблема:** В функциях `applyAdaMfgUnlock`, `prepareAmpereFgSpoof` и `applyAmpereMfgUnlock` вызов `std::string(state.detail.begin(), state.detail.end())` преобразует `wchar_t` в `char` внутри шаблона `std::format`, вызывая краш оптимизатора MSVC.
   * **Исправление:** Заменять на явное копирование:
     ```cpp
     std::string detailStr;
     detailStr.reserve(state.detail.size());
     for (auto c : state.detail) detailStr.push_back(static_cast<char>(c));
     // и передавать detailStr в std::format(...)
     ```
2. **`include/veyra/diagnostics/FrameTrace.h` (Ошибка C1001 `msc1.cpp:1589`)**:
   * **Проблема:** Строка `std::array<FrameTraceEvent,capacity> events_{};` инициализирует нулями массив из 8192 структур в заголовке, что подвешивает компилятор.
   * **Исправление:** Убрать фигурные скобки:
     ```cpp
     std::array<FrameTraceEvent,capacity> events_;
     ```
3. **`CMakeLists.txt` (Версия SDL3)**:
   * **Проблема:** `find_package(SDL3 3.4.14 EXACT CONFIG REQUIRED)` падает, если в vcpkg установлена версия `3.4.16`.
   * **Исправление:** Удалить слово `EXACT`:
     ```cmake
     find_package(SDL3 3.4.14 CONFIG REQUIRED)
     ```
4. **`CMakeLists.txt` (Утилиты FSR Probes)**:
   * Обернуть добавление `veyra_fsr_probe` и `veyra_fsr_upscale_probe` в условие проверки наличия каталога SDK 2.3.0:
     ```cmake
     if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party_local/amd/FidelityFX-SDK-2.3.0")
       add_executable(veyra_fsr_probe tools/fsr_probe/main.cpp)
       ...
     endif()
     ```

---

## 6. Пошаговый процесс компиляции

Запустите PowerShell и выполните команду из папки репозитория `C:\Users\Alexandr\Desktop\Veira\Veyra-NRVideo`:

```powershell
powershell.exe -ExecutionPolicy Bypass -File "scripts\build.ps1" `
  -Root "C:\Users\Alexandr\Desktop\Veira\Veyra-NRVideo" `
  -Preset "x64-release" `
  -RemotePlay `
  -ChiakiCheckout "C:\veyra-deps\chiaki-clean" `
  -ChiakiStage "C:\veyra-deps\chiaki-stage" `
  -RemotePlayPrefixPath "C:\vcpkg\installed\x64-windows-static" `
  -ProtocPath "C:\vcpkg\installed\x64-windows\tools\protobuf\protoc.exe" `
  -PkgConfigPath "C:\vcpkg\downloads\tools\msys2\3e71d1f8e22ab23f\mingw64\bin\pkgconf.exe" `
  -FfmpegRoot "C:\veyra-deps\ffmpeg-ps5-dav1d-installed"
```

Все бинарники будут скомпилированы в:
`C:\Users\Alexandr\Desktop\Veira\Veyra-NRVideo\out\build\x64-release\`

---

## 7. Комплектация готового Portable-пакета для переноса

Чтобы запустить собранную Veyra на любом компьютере без установки сред разработки:

1. **Корень папки (рядом с `veyra.exe`)**:
   * `avcodec-63.dll`, `avformat-63.dll`, `avutil-61.dll`, `swresample-7.dll`, `swscale-10.dll` (FFmpeg)
   * `dav1d.dll` (AV1 декодер)
   * `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll` (Visual C++ Runtime)
2. **Каталог `shaders\`**:
   * Скопировать целиком из `out\build\x64-release\shaders\` (содержит шейдеры масштабирования, цветопередачи, четкости).
3. **Каталог `runtime_local\` (или `runtime\`)**:
   * `runtime_local\config\ngx-local.json` с GUID-идентификатором проекта:
     ```json
     {
       "ngxProjectId": "a0f57b54-1daf-4934-90ae-c4035c19df04",
       "engineVersion": "1.4.4"
     }
     ```
   * `runtime_local\nvidia\`:
     * `nvngx_dlss.dll` — DLSS Super Resolution
     * `nvngx_dlssg.dll` — DLSS Frame Generation
     * `nvngx_dlssnr.dll` — базовая нейросеть шумоподавления / Ray Reconstruction
     * `nvngx_truehdr.dll` — RTX Video HDR
     * `nvngx_vsr.dll` — RTX Video Super Resolution
     * `nr-ampere\nvngx_dlssnr.dll` — версия NR для карт серии RTX 30
     * `nr-community\nvngx_dlssnr.dll` — комьюнити-версия NR для карт RTX 40/50
   * `runtime_local\intel\experimental\`:
     * `libxell.dll`, `libxess.dll`, `libxess_dx11.dll`, `libxess_fg.dll`
   * `runtime_local\amd\fidelityfx\`:
     * `amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_upscaler_dx12.dll`, `amd_fidelityfx_framegeneration_dx12.dll`

*(Все эти файлы рантайма можно взять из официального архива `Veyra-1.4.4-win64-portable.zip`).*

---

## 8. Проверка работоспособности после обновления

После завершения сборки протестируйте активацию нейросетей командой:

```powershell
& "out\build\x64-release\veyra_nr_runtime_switch_tests.exe" "REAMDE MP4.mp4" "out\test_nr_check"
```

Если все 3 режима DLSS-NR выдают `PASS selected runtime produces NR output`, приложение полностью готово к использованию.
