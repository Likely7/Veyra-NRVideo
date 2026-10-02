# SoundTouch 2.4.1, LGPL-2.1. Fixed source identity: VEYRA_ORIGIN.md.
add_library(veyra_soundtouch STATIC
  source/SoundTouch/AAFilter.cpp
  source/SoundTouch/BPMDetect.cpp
  source/SoundTouch/cpu_detect_x86.cpp
  source/SoundTouch/FIFOSampleBuffer.cpp
  source/SoundTouch/FIRFilter.cpp
  source/SoundTouch/InterpolateCubic.cpp
  source/SoundTouch/InterpolateLinear.cpp
  source/SoundTouch/InterpolateShannon.cpp
  source/SoundTouch/mmx_optimized.cpp
  source/SoundTouch/PeakFinder.cpp
  source/SoundTouch/RateTransposer.cpp
  source/SoundTouch/SoundTouch.cpp
  source/SoundTouch/sse_optimized.cpp
  source/SoundTouch/TDStretch.cpp)
target_include_directories(veyra_soundtouch PUBLIC include)
target_compile_definitions(veyra_soundtouch PUBLIC SOUNDTOUCH_FLOAT_SAMPLES)
target_compile_features(veyra_soundtouch PRIVATE cxx_std_17)
