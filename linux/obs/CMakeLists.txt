# Veyra OBS filter module.
#
# OBS owns capture, decode, encode and composition; this module adds only the
# per-frame GPU enhancement chain, which is why it works on Linux without any
# of the Windows-only backends (NGX/DLSS, Media Foundation, WGC, WASAPI).

find_package(libobs REQUIRED)

add_library(veyra_obs_plugin MODULE
  veyra-filter.c
)
set_target_properties(veyra_obs_plugin PROPERTIES
  PREFIX ""
  OUTPUT_NAME "veyra-filter"
)
target_compile_features(veyra_obs_plugin PRIVATE c_std_11)
target_link_libraries(veyra_obs_plugin PRIVATE OBS::libobs)

install(TARGETS veyra_obs_plugin
  LIBRARY DESTINATION lib/obs-plugins)
install(FILES veyra.effect
  DESTINATION share/obs/obs-plugins/veyra-filter)
install(DIRECTORY locale/
  DESTINATION share/obs/obs-plugins/veyra-filter/locale)

include(CTest)
if(BUILD_TESTING)
  add_executable(veyra_cube_tests ../../tests/obs-filter/cube-tests.c)
  target_link_libraries(veyra_cube_tests PRIVATE m)
  add_test(NAME obs_cube_parser COMMAND veyra_cube_tests "${CMAKE_CURRENT_BINARY_DIR}/test.cube")
  set_tests_properties(obs_cube_parser PROPERTIES TIMEOUT 30)
endif()
