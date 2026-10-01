include_guard(GLOBAL)
# Xbox home streaming (unofficial): sign-in, the streaming session API and a native WebRTC session.
#
#   veyra_xbox_protocol   WinHTTP + nlohmann-json for the services, libdatachannel (MPL-2.0) for WebRTC.
#
# libdatachannel, libjuice, usrsctp, libsrtp and plog come from the vcpkg tree on CMAKE_PREFIX_PATH
# (x64-windows-static, the same OpenSSL as the rest of the build). Nothing is downloaded here.
find_package(LibDataChannel CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(veyra_xbox_protocol STATIC
  "${VEYRA_ROOT}/src/xbox/Https.cpp"
  "${VEYRA_ROOT}/src/xbox/Account.cpp"
  "${VEYRA_ROOT}/src/xbox/StreamApi.cpp"
  "${VEYRA_ROOT}/src/xbox/WebRtcSession.cpp")
target_compile_features(veyra_xbox_protocol PUBLIC cxx_std_20)
set_target_properties(veyra_xbox_protocol PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(veyra_xbox_protocol PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(veyra_xbox_protocol PUBLIC "${VEYRA_ROOT}/include")
target_link_libraries(veyra_xbox_protocol
  PUBLIC LibDataChannel::LibDataChannel nlohmann_json::nlohmann_json veyra_base
  PRIVATE winhttp crypt32 shell32 ole32 ws2_32)
veyra_apply_warnings(veyra_xbox_protocol)

add_executable(veyra_xbox_tests "${VEYRA_ROOT}/tests/xbox/XboxTests.cpp")
target_compile_features(veyra_xbox_tests PRIVATE cxx_std_20)
target_compile_definitions(veyra_xbox_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(veyra_xbox_tests PRIVATE veyra_xbox_protocol)
veyra_apply_warnings(veyra_xbox_tests)
