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
        "${VEYRA_ROOT}/src/media/FfmpegLog.cpp"
        "${VEYRA_ROOT}/src/source/RemotePlaySource.cpp")
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
