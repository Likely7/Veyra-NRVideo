include_guard(GLOBAL)
include(FetchContent)
set(mapper_pin 826547879659100e5e3b3e4c975f205d15cf66fb)
FetchContent_Declare(stickmouse
    GIT_REPOSITORY https://github.com/tprjd/stickmouse-mapper.git
    GIT_TAG "${mapper_pin}")
FetchContent_MakeAvailable(stickmouse)
if(EXISTS "${stickmouse_SOURCE_DIR}/.git")
    find_package(Git REQUIRED)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${stickmouse_SOURCE_DIR}" OUTPUT_VARIABLE mapper_revision
        OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
        WORKING_DIRECTORY "${stickmouse_SOURCE_DIR}" OUTPUT_VARIABLE mapper_changes
        OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    if(NOT mapper_revision STREQUAL mapper_pin OR mapper_changes)
        message(FATAL_ERROR "Stickmouse Mapper checkout does not match the PS5 profile revision")
    endif()
else()
    # The corresponding-source archive contains the pinned Git blobs with their original line endings.
    file(GLOB_RECURSE mapper_paths LIST_DIRECTORIES false RELATIVE "${stickmouse_SOURCE_DIR}" "${stickmouse_SOURCE_DIR}/*")
    list(SORT mapper_paths)
    set(mapper_manifest "")
    foreach(path IN LISTS mapper_paths)
        file(SHA256 "${stickmouse_SOURCE_DIR}/${path}" file_hash)
        string(APPEND mapper_manifest "${file_hash}  ${path}\n")
    endforeach()
    string(SHA256 manifest_hash "${mapper_manifest}")
    if(NOT manifest_hash STREQUAL "3dd9ce3e0d8ad4ec29c275436d622b2177fc2859bd0f3cbf3dde27bc16c87bc8")
        message(FATAL_ERROR "Extracted Stickmouse Mapper source does not match the pinned revision")
    endif()
endif()
find_program(VEYRA_CARGO cargo REQUIRED)
set(mapper_target "${CMAKE_CURRENT_BINARY_DIR}/stickmouse-target")
set(mapper_dll "${mapper_target}/release/stickmouse_mapper.dll")
set(mapper_lib "${mapper_dll}.lib")
file(GLOB_RECURSE mapper_inputs CONFIGURE_DEPENDS
    "${stickmouse_SOURCE_DIR}/crates/*.rs" "${stickmouse_SOURCE_DIR}/crates/*/Cargo.toml")
add_custom_command(OUTPUT "${mapper_dll}" "${mapper_lib}"
    COMMAND "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${mapper_target}" CARGO_BUILD_JOBS=2
            "${VEYRA_CARGO}" build --locked --release -p stickmouse-mapper-c
    WORKING_DIRECTORY "${stickmouse_SOURCE_DIR}"
    DEPENDS ${mapper_inputs} "${stickmouse_SOURCE_DIR}/Cargo.lock" "${stickmouse_SOURCE_DIR}/Cargo.toml"
    VERBATIM)
add_custom_target(veyra_build_mapper DEPENDS "${mapper_dll}" "${mapper_lib}")
add_library(veyra_stickmouse SHARED IMPORTED GLOBAL)
set_target_properties(veyra_stickmouse PROPERTIES
    IMPORTED_LOCATION "${mapper_dll}" IMPORTED_IMPLIB "${mapper_lib}"
    INTERFACE_INCLUDE_DIRECTORIES "${stickmouse_SOURCE_DIR}/crates/stickmouse-mapper-c/include")
add_dependencies(veyra_stickmouse veyra_build_mapper)
set(VEYRA_MAPPER_PROFILE "${stickmouse_SOURCE_DIR}/crates/stickmouse-mapper/examples/gta-vi-ps5-0.6.87-calibrated.v2.json")
function(veyra_stage_mapper target)
    set(dir "$<TARGET_FILE_DIR:${target}>")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:veyra_stickmouse>" "${dir}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${dir}/profiles" "${dir}/licenses/stickmouse"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${VEYRA_MAPPER_PROFILE}" "${dir}/profiles"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${stickmouse_SOURCE_DIR}/LICENSE" "${stickmouse_SOURCE_DIR}/NOTICE.md" "${dir}/licenses/stickmouse"
        VERBATIM)
endfunction()
