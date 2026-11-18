cmake_minimum_required(VERSION 3.24)
project(veyra_feedback_history_regression C)

set(CHIAKI_SOURCE "" CACHE PATH "Pinned clean or patched Chiaki source")
option(LEGACY_CHIAKI "Test the original history API as a negative control" OFF)

if(NOT WIN32)
    message(FATAL_ERROR "These standalone sender tests require Windows")
endif()
if(NOT EXISTS "${CHIAKI_SOURCE}/lib/include/chiaki/feedbacksender.h")
    message(FATAL_ERROR "Set CHIAKI_SOURCE to a pinned clean or patched Chiaki source tree")
endif()

add_executable(feedback-history-regression
    sender.c
    "${CHIAKI_SOURCE}/lib/src/feedbacksender.c"
    "${CHIAKI_SOURCE}/lib/src/feedback.c"
    "${CHIAKI_SOURCE}/lib/src/controller.c"
    "${CHIAKI_SOURCE}/lib/src/thread.c"
    "${CHIAKI_SOURCE}/lib/src/time.c"
    "${CHIAKI_SOURCE}/lib/src/log.c")
target_include_directories(feedback-history-regression PRIVATE "${CHIAKI_SOURCE}/lib/include")
target_compile_definitions(feedback-history-regression PRIVATE _CRT_SECURE_NO_WARNINGS)
if(LEGACY_CHIAKI)
    target_compile_definitions(feedback-history-regression PRIVATE LEGACY_CHIAKI)
endif()
target_link_libraries(feedback-history-regression PRIVATE ws2_32)

enable_testing()
add_test(NAME feedback-history-regression COMMAND feedback-history-regression)
set_tests_properties(feedback-history-regression PROPERTIES TIMEOUT 15)

if(NOT LEGACY_CHIAKI)
    add_executable(upstream-feedback-regression upstream.c
        "${CHIAKI_SOURCE}/test/feedback.c"
        "${CHIAKI_SOURCE}/test/test_log.c"
        "${CHIAKI_SOURCE}/test/munit/munit.c"
        "${CHIAKI_SOURCE}/lib/src/feedback.c"
        "${CHIAKI_SOURCE}/lib/src/controller.c"
        "${CHIAKI_SOURCE}/lib/src/log.c")
    target_include_directories(upstream-feedback-regression PRIVATE
        "${CHIAKI_SOURCE}/lib/include" "${CHIAKI_SOURCE}/test/munit")
    target_compile_definitions(upstream-feedback-regression PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(upstream-feedback-regression PRIVATE ws2_32)
    add_test(NAME upstream-feedback-regression COMMAND upstream-feedback-regression)
    set_tests_properties(upstream-feedback-regression PROPERTIES TIMEOUT 15)
endif()
