add_executable(veyra_remoteplay_native_probe "${CMAKE_CURRENT_LIST_DIR}/native_main.cpp")
target_compile_features(veyra_remoteplay_native_probe PRIVATE cxx_std_20)
target_compile_definitions(veyra_remoteplay_native_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(veyra_remoteplay_native_probe PRIVATE veyra_remoteplay_chiaki chiaki-lib)
add_test(NAME rp.native_core_initialization COMMAND veyra_remoteplay_native_probe)
set_tests_properties(rp.native_core_initialization PROPERTIES TIMEOUT 30 LABELS "native-core;not-ps5")

# Optional source regressions link the production adapter and real FFmpeg.
set(VEYRA_RP_FFMPEG_ROOT "" CACHE PATH "Existing Veyra FFmpeg installation")
if(VEYRA_RP_FFMPEG_ROOT)
    add_executable(veyra_remoteplay_source_tests
        "${VEYRA_ROOT}/tests/remoteplay/SourceTests.cpp"
        "${VEYRA_ROOT}/src/source/RemotePlaySource.cpp"
        "${VEYRA_ROOT}/src/base/Log.cpp")
    target_compile_features(veyra_remoteplay_source_tests PRIVATE cxx_std_20)
    target_compile_definitions(veyra_remoteplay_source_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(veyra_remoteplay_source_tests SYSTEM PRIVATE "${VEYRA_RP_FFMPEG_ROOT}/include")
    target_link_libraries(veyra_remoteplay_source_tests PRIVATE veyra_remoteplay_chiaki d3d12
        "${VEYRA_RP_FFMPEG_ROOT}/lib/avcodec.lib"
        "${VEYRA_RP_FFMPEG_ROOT}/lib/avutil.lib"
        "${VEYRA_RP_FFMPEG_ROOT}/lib/avformat.lib")
endif()

add_executable(veyra_remoteplay_profile_tests "${VEYRA_ROOT}/tests/remoteplay/ProfileStoreTests.cpp")
target_compile_features(veyra_remoteplay_profile_tests PRIVATE cxx_std_20)
target_compile_definitions(veyra_remoteplay_profile_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(veyra_remoteplay_profile_tests PRIVATE veyra_remoteplay_chiaki)
add_test(NAME rp.dpapi_profile COMMAND veyra_remoteplay_profile_tests)
set_tests_properties(rp.dpapi_profile PROPERTIES TIMEOUT 30)

option(VEYRA_RP_BUILD_PSN_TESTS "Build PSN OAuth service tests with Qt" OFF)
if(VEYRA_RP_BUILD_PSN_TESTS)
    include("${VEYRA_ROOT}/cmake/VeyraPsnService.cmake")
endif()

# Compile the exact patched holepunch implementation into a lifetime regression
# executable so it can exercise an unopened websocket worker without PSN I/O.
add_executable(veyra_remoteplay_psn_cancellation_tests
    "${VEYRA_ROOT}/tests/remoteplay/PsnCancellationTests.c")
target_compile_definitions(veyra_remoteplay_psn_cancellation_tests PRIVATE
    MINIUPNP_STATICLIB
    "VEYRA_CHIAKI_HOLEPUNCH_SOURCE=\"${VEYRA_RP_CHIAKI_SOURCE_DIR}/lib/src/remote/holepunch.c\"")
target_include_directories(veyra_remoteplay_psn_cancellation_tests PRIVATE
    "$<TARGET_PROPERTY:chiaki-lib,INCLUDE_DIRECTORIES>")
target_link_libraries(veyra_remoteplay_psn_cancellation_tests PRIVATE chiaki-lib)
add_test(NAME rp.psn_cancellation COMMAND veyra_remoteplay_psn_cancellation_tests)
set_tests_properties(rp.psn_cancellation PROPERTIES TIMEOUT 30 LABELS "native-core;offline;not-ps5")
