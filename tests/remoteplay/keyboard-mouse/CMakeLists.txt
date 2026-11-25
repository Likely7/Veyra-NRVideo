cmake_minimum_required(VERSION 3.24)
project(VeyraKeyboardMouseChecks LANGUAGES CXX)
get_filename_component(VEYRA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include("${VEYRA_ROOT}/cmake/VeyraStickmouse.cmake")
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickTest)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
add_compile_options(/utf-8)
add_library(input STATIC "${VEYRA_ROOT}/src/remoteplay/KeyboardMouse.cpp")
target_include_directories(input PUBLIC "${VEYRA_ROOT}/include")
target_link_libraries(input PUBLIC veyra_stickmouse)
add_executable(controller ControllerTests.cpp)
target_link_libraries(controller PRIVATE input)
veyra_stage_mapper(controller)
add_library(test_log STATIC "${VEYRA_ROOT}/src/base/Log.cpp" "${VEYRA_ROOT}/src/base/PrivateFrameTrace.cpp")
target_include_directories(test_log PUBLIC "${VEYRA_ROOT}/include")
add_executable(editor EditorTests.cpp "${VEYRA_ROOT}/src/ui/Ps5KeyboardSettings.cpp"
    "${VEYRA_ROOT}/src/ui/UiLanguage.cpp"
    "${VEYRA_ROOT}/src/ui/Ps5PlayerShortcuts.cpp"
    "${VEYRA_ROOT}/include/veyra/ui/Ps5KeyboardSettings.h"
    "${VEYRA_ROOT}/src/ui/WindowInputCapture.cpp" "${VEYRA_ROOT}/include/veyra/ui/WindowInputCapture.h")
target_sources(editor PRIVATE "${VEYRA_ROOT}/src/ui/Ps5InputDevices.cpp" "${VEYRA_ROOT}/include/veyra/ui/Ps5InputDevices.h")
target_link_libraries(editor PRIVATE input test_log Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickTest cfgmgr32)
qt_add_resources(editor "editor_catalog" PREFIX "/i18n" BASE "${VEYRA_ROOT}/i18n" FILES "${VEYRA_ROOT}/i18n/catalog.json")
target_compile_definitions(editor PRIVATE VEYRA_TEST_QML="${VEYRA_ROOT}/qml")
veyra_stage_mapper(editor)
enable_testing()
add_test(NAME ps5-mapper-controller COMMAND controller)
add_test(NAME ps5-mapper-editor COMMAND editor -input "${CMAKE_CURRENT_LIST_DIR}/qml" -platform offscreen)
set_tests_properties(ps5-mapper-controller PROPERTIES TIMEOUT 60)
set_tests_properties(ps5-mapper-editor PROPERTIES TIMEOUT 180)

# The owner test uses Veyra's FFmpeg source adapter and an in-process console.
if(VEYRA_FFMPEG_ROOT)
    find_library(OWNER_AVCODEC avcodec PATHS "${VEYRA_FFMPEG_ROOT}/lib" REQUIRED NO_DEFAULT_PATH)
    find_library(OWNER_AVUTIL avutil PATHS "${VEYRA_FFMPEG_ROOT}/lib" REQUIRED NO_DEFAULT_PATH)
    add_library(test_console STATIC TestConsole.cpp
        "${VEYRA_ROOT}/src/source/RemotePlaySessionSource.cpp"
        "${VEYRA_ROOT}/src/source/RemotePlaySource.cpp"
        "${VEYRA_ROOT}/src/remoteplay/SessionInbox.cpp"
        "${VEYRA_ROOT}/src/remoteplay/Timeline.cpp"
        "${VEYRA_ROOT}/src/remoteplay/VideoIngress.cpp"
        "${VEYRA_ROOT}/src/remoteplay/Validation.cpp"
        "${VEYRA_ROOT}/src/remoteplay/AnnexB.cpp"
        "${VEYRA_ROOT}/src/media/FfmpegLog.cpp")
    target_include_directories(test_console PUBLIC "${VEYRA_FFMPEG_ROOT}/include")
    target_link_libraries(test_console PUBLIC input test_log Qt6::Core "${OWNER_AVCODEC}" "${OWNER_AVUTIL}")
    add_executable(owner OwnerTests.cpp)
    target_link_libraries(owner PRIVATE test_console)
    target_link_libraries(editor PRIVATE test_console)
    target_compile_definitions(editor PRIVATE VEYRA_TEST_CONSOLE)
    set_tests_properties(ps5-mapper-editor PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${VEYRA_FFMPEG_ROOT}/bin")
    veyra_stage_mapper(owner)
    add_custom_command(TARGET owner POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${stickmouse_SOURCE_DIR}/crates/stickmouse-mapper/tests/fixtures/recorded-aircraft/rounds.json"
        "$<TARGET_FILE_DIR:owner>/rounds.json")
    foreach(scenario ownership vehicle manual haptics)
        add_test(NAME ps5-mapper-owner-${scenario} COMMAND owner ${scenario})
        set_tests_properties(ps5-mapper-owner-${scenario} PROPERTIES TIMEOUT 80
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${VEYRA_FFMPEG_ROOT}/bin")
    endforeach()
endif()
