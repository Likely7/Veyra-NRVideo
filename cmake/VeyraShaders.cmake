# Build-time HLSL compilation via the installed DXC (Playbook section 5.4).
# Machine-specific DXC discovery stays here; CMakePresets stay path-free.

function(veyra_find_dxc out_var)
  set(candidates "")
  file(GLOB kit_roots "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/*")
  if(kit_roots)
    list(SORT kit_roots ORDER DESCENDING)
    foreach(root IN LISTS kit_roots)
      if(EXISTS "${root}/x64/dxc.exe")
        list(APPEND candidates "${root}/x64/dxc.exe")
      endif()
    endforeach()
  endif()
  if(NOT candidates)
    message(FATAL_ERROR "veyra_find_dxc: dxc.exe not found under the Windows Kits")
  endif()
  list(GET candidates 0 chosen)
  set(${out_var} "${chosen}" PARENT_SCOPE)
endfunction()

# Shared includes (.hlsli) that every shader may pull in. They MUST be declared
# as dependencies: a colour-grade change that only edits ColorGrade.hlsli used
# to leave the .dxil untouched, so the build silently kept running the old
# shader - the kind of no-op that makes "I changed the shader" unverifiable.
function(veyra_shader_includes out_var)
  file(GLOB includes "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.hlsli")
  list(SORT includes)
  set(${out_var} "${includes}" PARENT_SCOPE)
endfunction()

# Compiles one compute shader into <binary-dir>/shaders/<name>.dxil.
# Single-config Ninja: branch on CMAKE_BUILD_TYPE instead of generator
# expressions, whose empty expansion would quote-separate dxc arguments.
function(veyra_add_compute_shader target_name shader_name)
  veyra_find_dxc(DXC_EXE)
  veyra_shader_includes(SHADER_INCLUDES)
  set(source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader_name}.hlsl")
  set(output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader_name}.dxil")
  set(config_flags -O3 -Qstrip_debug -Qstrip_reflect)
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(config_flags -Od -Zi -Qembed_debug)
  endif()
  add_custom_command(
    OUTPUT "${output}"
    COMMAND "${DXC_EXE}" -T cs_6_0 -E main ${config_flags}
            -Fo "${output}" "${source}"
    DEPENDS "${source}" ${SHADER_INCLUDES}
    COMMENT "dxc ${shader_name}.hlsl (${CMAKE_BUILD_TYPE})"
    VERBATIM)
  add_custom_target(${target_name} DEPENDS "${output}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}" ${SHADER_INCLUDES})
endfunction()

# Compiles one compute shader source with preprocessor defines into
# <binary-dir>/shaders/<output_name>.dxil: several presets of one source (the
# anti-aliasing quality tiers). Extra arguments are NAME or NAME=VALUE defines.
function(veyra_add_compute_shader_variant target_name shader_name output_name)
  veyra_find_dxc(DXC_EXE)
  veyra_shader_includes(SHADER_INCLUDES)
  set(source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader_name}.hlsl")
  set(output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${output_name}.dxil")
  set(config_flags -O3 -Qstrip_debug -Qstrip_reflect)
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(config_flags -Od -Zi -Qembed_debug)
  endif()
  set(defines "")
  foreach(define IN LISTS ARGN)
    list(APPEND defines "-D${define}")
  endforeach()
  add_custom_command(
    OUTPUT "${output}"
    COMMAND "${DXC_EXE}" -T cs_6_0 -E main ${config_flags} ${defines}
            -Fo "${output}" "${source}"
    DEPENDS "${source}" ${SHADER_INCLUDES}
    COMMENT "dxc ${shader_name}.hlsl -> ${output_name}.dxil (${CMAKE_BUILD_TYPE})"
    VERBATIM)
  add_custom_target(${target_name} DEPENDS "${output}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}" ${SHADER_INCLUDES})
endfunction()

# Compiles a vertex+pixel shader pair (entry vsMain/psMain) into two DXIL
# files under <binary-dir>/shaders/. Used by the graphics present path.
function(veyra_add_graphics_shader_pair target_name shader_name)
  veyra_find_dxc(DXC_EXE)
  veyra_shader_includes(SHADER_INCLUDES)
  set(source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader_name}.hlsl")
  set(vs_out "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader_name}_vs.dxil")
  set(ps_out "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader_name}_ps.dxil")
  set(config_flags -O3 -Qstrip_debug -Qstrip_reflect)
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(config_flags -Od -Zi -Qembed_debug)
  endif()
  add_custom_command(
    OUTPUT "${vs_out}"
    COMMAND "${DXC_EXE}" -T vs_6_0 -E vsMain ${config_flags}
            -Fo "${vs_out}" "${source}"
    DEPENDS "${source}" ${SHADER_INCLUDES}
    COMMENT "dxc ${shader_name}.hlsl VS (${CMAKE_BUILD_TYPE})"
    VERBATIM)
  add_custom_command(
    OUTPUT "${ps_out}"
    COMMAND "${DXC_EXE}" -T ps_6_0 -E psMain ${config_flags}
            -Fo "${ps_out}" "${source}"
    DEPENDS "${source}" ${SHADER_INCLUDES}
    COMMENT "dxc ${shader_name}.hlsl PS (${CMAKE_BUILD_TYPE})"
    VERBATIM)
  add_custom_target(${target_name} DEPENDS "${vs_out}" "${ps_out}")
endfunction()
