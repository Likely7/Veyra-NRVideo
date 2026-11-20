cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(veyra_hdmi_bitstream_tests LANGUAGES CXX)
  enable_testing()
endif()

set(veyra_root "../..")
add_executable(veyra_hdmi_bitstream_tests
  ${veyra_root}/tests/unit/HdmiBitstreamTests.cpp
  ${veyra_root}/src/sink/HdmiBitstreamOutput.cpp
  ${veyra_root}/src/sink/Iec61937Burst.cpp)
target_compile_features(veyra_hdmi_bitstream_tests PRIVATE cxx_std_20)
target_compile_definitions(veyra_hdmi_bitstream_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(veyra_hdmi_bitstream_tests PRIVATE ${veyra_root}/include)
target_link_libraries(veyra_hdmi_bitstream_tests PRIVATE ole32 mmdevapi avrt)
foreach(case IN ITEMS ac3 dts underrun endpoint format-change missing-device)
  add_test(NAME veyra_hdmi_bitstream_${case} COMMAND veyra_hdmi_bitstream_tests ${case})
endforeach()
