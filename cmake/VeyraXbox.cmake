include_guard(GLOBAL)
# Xbox home streaming (unofficial): sign-in and the streaming session API.
# The media plane is the Chromium reference player (VeyraXboxWeb.cmake); the
# old native libdatachannel session was removed.
#
#   veyra_xbox_protocol   WinHTTP + nlohmann-json for the services.
#
# Everything comes from the vcpkg tree on CMAKE_PREFIX_PATH (x64-windows-static,
# the same OpenSSL as the rest of the build). Nothing is downloaded here.
find_package(nlohmann_json CONFIG REQUIRED)

add_library(veyra_xbox_protocol STATIC
  "${VEYRA_ROOT}/src/xbox/Https.cpp"
  "${VEYRA_ROOT}/src/xbox/StreamProfile.cpp"
  "${VEYRA_ROOT}/src/xbox/Account.cpp"
  "${VEYRA_ROOT}/src/xbox/StreamApi.cpp")
target_compile_features(veyra_xbox_protocol PUBLIC cxx_std_20)
set_target_properties(veyra_xbox_protocol PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(veyra_xbox_protocol PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(veyra_xbox_protocol PUBLIC "${VEYRA_ROOT}/include")
target_link_libraries(veyra_xbox_protocol
  PUBLIC nlohmann_json::nlohmann_json veyra_base
  PRIVATE winhttp crypt32 shell32 ole32 ws2_32)
veyra_apply_warnings(veyra_xbox_protocol)

add_executable(veyra_xbox_tests "${VEYRA_ROOT}/tests/xbox/XboxTests.cpp")
target_compile_features(veyra_xbox_tests PRIVATE cxx_std_20)
target_compile_definitions(veyra_xbox_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(veyra_xbox_tests PRIVATE veyra_xbox_protocol)
veyra_apply_warnings(veyra_xbox_tests)

# v2 gamepad bridge mapping (pure C++, no WebView2): pins the ControllerState ->
# JS InputFrame conversion as the exact inverse of gamepadFromController. Built
# whenever Xbox is enabled so the contract is checked even without the v2 engine.
add_executable(veyra_xbox_web_gamepad_tests "${VEYRA_ROOT}/tests/xbox/XboxWebGamepadTests.cpp")
target_compile_features(veyra_xbox_web_gamepad_tests PRIVATE cxx_std_20)
target_compile_definitions(veyra_xbox_web_gamepad_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(veyra_xbox_web_gamepad_tests PRIVATE veyra_xbox_protocol)
veyra_apply_warnings(veyra_xbox_web_gamepad_tests)
