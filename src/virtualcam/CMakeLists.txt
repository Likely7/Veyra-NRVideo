cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(veyra_virtual_camera LANGUAGES C CXX)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
endif()
set(VCAM_VENDOR "${CMAKE_CURRENT_LIST_DIR}/../../third_party/obs_virtualcam")
set(VCAM_LIBDS "${VCAM_VENDOR}/libdshowcapture")
set(VCAM_IPC_SOURCES "${VCAM_VENDOR}/ipc/shared-memory-queue.c" "${VCAM_VENDOR}/ipc/tiny-nv12-scale.c")
add_library(veyra_virtualcam_module MODULE
  ${VCAM_VENDOR}/module/virtualcam-module.cpp
  ${VCAM_VENDOR}/module/virtualcam-filter.cpp
  ${VCAM_VENDOR}/module/sleepto.c
  ${VCAM_LIBDS}/source/output-filter.cpp
  ${VCAM_LIBDS}/source/dshow-formats.cpp
  ${VCAM_LIBDS}/source/dshow-media-type.cpp
  ${VCAM_LIBDS}/source/log.cpp
  ${VCAM_LIBDS}/source/dshow-error.cpp
  ${VCAM_IPC_SOURCES}
  CameraRegistration.cpp CameraPlaceholder.cpp virtualcam.def)
target_include_directories(veyra_virtualcam_module PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${VCAM_VENDOR}" "${VCAM_VENDOR}/ipc" "${VCAM_LIBDS}")
target_compile_definitions(veyra_virtualcam_module PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_features(veyra_virtualcam_module PRIVATE cxx_std_20)
target_compile_options(veyra_virtualcam_module PRIVATE /utf-8 /W3 /WX-)
target_link_libraries(veyra_virtualcam_module PRIVATE strmiids ole32 oleaut32 winmm advapi32 setupapi cfgmgr32)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(VCAM_BITS 64)
else()
  set(VCAM_BITS 32)
endif()
set_target_properties(veyra_virtualcam_module PROPERTIES OUTPUT_NAME "veyra-virtualcam${VCAM_BITS}" PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/virtualcam" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/virtualcam"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/virtualcam" LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/virtualcam"
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_library(veyra_virtualcam_transport STATIC ${VCAM_IPC_SOURCES})
  target_include_directories(veyra_virtualcam_transport PUBLIC "${VCAM_VENDOR}/ipc")
  target_compile_definitions(veyra_virtualcam_transport PRIVATE UNICODE _UNICODE)
  include(ExternalProject)
  ExternalProject_Add(veyra_virtualcam_x86
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/virtualcam-x86"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
    BUILD_ALWAYS TRUE
    CMAKE_GENERATOR "Visual Studio 17 2022"
    CMAKE_GENERATOR_PLATFORM Win32
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release --parallel 1 --target veyra_virtualcam_module
    INSTALL_COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      <BINARY_DIR>/virtualcam/veyra-virtualcam32.dll "${CMAKE_BINARY_DIR}/virtualcam/veyra-virtualcam32.dll")
  set(VCAM_HASH_HEADER "${CMAKE_CURRENT_BINARY_DIR}/CameraPayloadHashes.h")
  add_custom_command(OUTPUT "${VCAM_HASH_HEADER}"
    COMMAND "${CMAKE_COMMAND}" "-DVCAM64=$<TARGET_FILE:veyra_virtualcam_module>"
      "-DVCAM32=${CMAKE_BINARY_DIR}/virtualcam/veyra-virtualcam32.dll" "-DOUTPUT=${VCAM_HASH_HEADER}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/GeneratePayloadHashes.cmake"
    DEPENDS veyra_virtualcam_module veyra_virtualcam_x86 "${CMAKE_CURRENT_SOURCE_DIR}/GeneratePayloadHashes.cmake")
  add_executable(veyra_virtualcam_setup CameraSetup.cpp CameraRegistration.cpp "${VCAM_HASH_HEADER}")
  target_include_directories(veyra_virtualcam_setup PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_compile_features(veyra_virtualcam_setup PRIVATE cxx_std_20)
  target_compile_options(veyra_virtualcam_setup PRIVATE /utf-8)
  target_compile_definitions(veyra_virtualcam_setup PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(veyra_virtualcam_setup PRIVATE advapi32 bcrypt shell32 ole32 oleaut32 uuid user32 strmiids)
  set_target_properties(veyra_virtualcam_setup PROPERTIES OUTPUT_NAME "veyra-camera-setup"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/virtualcam" MSVC_RUNTIME_LIBRARY "MultiThreaded")
  add_custom_target(veyra_virtualcam_components DEPENDS veyra_virtualcam_setup)
endif()

foreach(kind probe transport_tests cadence)
  if(kind STREQUAL probe)
    set(VCAM_TEST_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../tools/virtual_camera_probe/main.cpp")
  elseif(kind STREQUAL cadence)
    # What a DirectShow consumer receives: arrival times and pictures (2026-11-17).
    set(VCAM_TEST_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../tools/virtual_camera_cadence/main.cpp")
  else()
    set(VCAM_TEST_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../tests/unit/VirtualCameraTransportTests.cpp")
  endif()
  add_executable(veyra_virtualcam_${kind} EXCLUDE_FROM_ALL ${VCAM_TEST_SOURCE} ${VCAM_IPC_SOURCES} CameraRegistration.cpp)
  target_include_directories(veyra_virtualcam_${kind} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${VCAM_VENDOR}/ipc" "${CMAKE_CURRENT_LIST_DIR}/../../include")
  target_compile_definitions(veyra_virtualcam_${kind} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_features(veyra_virtualcam_${kind} PRIVATE cxx_std_20)
  target_compile_options(veyra_virtualcam_${kind} PRIVATE /utf-8 /W3 /WX-)
  target_link_libraries(veyra_virtualcam_${kind} PRIVATE strmiids ole32 oleaut32 advapi32)
  set_target_properties(veyra_virtualcam_${kind} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/virtualcam" RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/virtualcam"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endforeach()
