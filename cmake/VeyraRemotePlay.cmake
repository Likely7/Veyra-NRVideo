include_guard(GLOBAL)
find_package(Threads REQUIRED)
add_library(veyra_remoteplay_core STATIC
    "${VEYRA_ROOT}/src/remoteplay/Validation.cpp"
    "${VEYRA_ROOT}/src/remoteplay/AnnexB.cpp"
    "${VEYRA_ROOT}/src/remoteplay/Timeline.cpp"
    "${VEYRA_ROOT}/src/remoteplay/VideoIngress.cpp"
    "${VEYRA_ROOT}/src/remoteplay/SessionInbox.cpp")
target_include_directories(veyra_remoteplay_core PUBLIC "${VEYRA_ROOT}/include")
target_compile_features(veyra_remoteplay_core PUBLIC cxx_std_20)
set_target_properties(veyra_remoteplay_core PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(veyra_remoteplay_core PUBLIC Threads::Threads)
# Shared pinned native dependency for the product and probes. No stub fallback.
enable_language(C)
set(VEYRA_RP_CHIAKI_SOURCE_DIR "" CACHE PATH "Patched chiaki-ng staging source tree")
set(VEYRA_RP_CHIAKI_VERIFY_DIR "${VEYRA_RP_CHIAKI_SOURCE_DIR}" CACHE PATH "Clean pinned chiaki-ng checkout used to verify provenance")
if(NOT EXISTS "${VEYRA_RP_CHIAKI_SOURCE_DIR}/lib/include/chiaki/session.h")
    message(FATAL_ERROR "VEYRA_RP_BUILD_NATIVE needs the pinned chiaki-ng source tree")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${VEYRA_ROOT}/scripts/remoteplay/verify-chiaki-stage.py"
        --stage "${VEYRA_RP_CHIAKI_SOURCE_DIR}" --clean "${VEYRA_RP_CHIAKI_VERIFY_DIR}"
    RESULT_VARIABLE CHIAKI_VERIFY_RESULT OUTPUT_VARIABLE CHIAKI_VERIFY_OUTPUT
    ERROR_VARIABLE CHIAKI_VERIFY_ERROR)
if(NOT CHIAKI_VERIFY_RESULT EQUAL 0)
    message(FATAL_ERROR "Chiaki source verification failed: ${CHIAKI_VERIFY_OUTPUT} ${CHIAKI_VERIFY_ERROR}")
endif()
# Target C++20 requirements remain explicit despite upstream defaults.
set(CHIAKI_ENABLE_GUI OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_CLI OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_ANDROID OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_BOREALIS OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_FFMPEG_DECODER OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_PI_DECODER OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_SETSU OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_STEAMDECK_NATIVE OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_STEAM_SHORTCUT OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_SPEEX OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_RUDP OFF CACHE STRING "" FORCE)
set(CHIAKI_LIB_ENABLE_OPUS ON CACHE BOOL "" FORCE)
set(CHIAKI_LIB_ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
set(CHIAKI_USE_SYSTEM_NANOPB OFF CACHE STRING "" FORCE)
set(CHIAKI_USE_SYSTEM_JERASURE OFF CACHE STRING "" FORCE)
set(CHIAKI_USE_SYSTEM_CURL OFF CACHE STRING "" FORCE)
# Disabling RUDP does NOT remove curl/json-c/libevent/miniupnpc in the pinned lib.
# Provision their REAL matching-toolchain dev libraries; never replace them with
# empty imported targets or remove FEC to force the native gate green.
add_subdirectory("${VEYRA_RP_CHIAKI_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/chiaki" EXCLUDE_FROM_ALL)
if(NOT TARGET chiaki-lib)
    message(FATAL_ERROR "Upstream chiaki-lib target was not created")
endif()
# vcpkg's static miniupnpc headers default to dllimport on Windows unless the
# caller opts into the static ABI. Without this definition the static library
# is found correctly but Chiaki emits unresolved __imp_* references.
target_compile_definitions(chiaki-lib PRIVATE MINIUPNP_STATICLIB)
add_library(veyra_remoteplay_chiaki STATIC
    "${VEYRA_ROOT}/src/remoteplay/ChiakiBackend.cpp"
    "${VEYRA_ROOT}/src/remoteplay/ChiakiRegistration.cpp"
    "${VEYRA_ROOT}/src/remoteplay/ProfileStore.cpp"
    "${VEYRA_ROOT}/src/remoteplay/Discovery.cpp")
target_compile_features(veyra_remoteplay_chiaki PUBLIC cxx_std_20)
target_compile_definitions(veyra_remoteplay_chiaki PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(veyra_remoteplay_chiaki PUBLIC veyra_remoteplay_core PRIVATE chiaki-lib crypt32 iphlpapi shell32 ole32 winhttp)
