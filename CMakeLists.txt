cmake_minimum_required(VERSION 3.25)
project(lollipop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(rexglue 0.10.1 EXACT REQUIRED CONFIG)

set(LOLLIPOP_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../LollipopChainsaw-private/generated"
    CACHE PATH "Private ReXGlue codegen output")
if(NOT EXISTS "${LOLLIPOP_GENERATED_DIR}/sources.cmake")
    message(FATAL_ERROR "Run rexglue codegen lollipop_manifest.toml first, or use build.ps1")
endif()
include("${LOLLIPOP_GENERATED_DIR}/sources.cmake")

add_custom_target(lollipop_codegen
    COMMAND $<TARGET_FILE:rex::rexglue> codegen "${CMAKE_CURRENT_SOURCE_DIR}/lollipop_manifest.toml"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)

add_library(lollipop_recomp OBJECT ${GENERATED_SOURCES})
target_include_directories(lollipop_recomp PRIVATE "${LOLLIPOP_GENERATED_DIR}")
target_link_libraries(lollipop_recomp PRIVATE rex::runtime)
target_precompile_headers(lollipop_recomp PRIVATE "${LOLLIPOP_GENERATED_DIR}/lollipop_pch.h")
rexglue_apply_target_settings(lollipop_recomp)
if(WIN32)
    if(MSVC)
        target_compile_options(lollipop_recomp PRIVATE /EHa)
    else()
        target_compile_options(lollipop_recomp PRIVATE -fasync-exceptions)
    endif()
endif()

add_executable(lollipop WIN32 src/main.cpp src/timing_hooks.cpp src/test_bridge.cpp)
if(WIN32)
    enable_language(RC)
    target_sources(lollipop PRIVATE assets/lollipop.rc)
    target_include_directories(lollipop PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()
target_include_directories(lollipop PRIVATE "${LOLLIPOP_GENERATED_DIR}")
target_link_libraries(lollipop PRIVATE lollipop_recomp rex::runtime)
rexglue_configure_target(lollipop GPU_PLUGINS xenos)

enable_testing()
add_test(NAME lollipop_input_identity
    COMMAND "${CMAKE_COMMAND}"
        "-DGAME_XEX=${CMAKE_CURRENT_SOURCE_DIR}/../LollipopChainsaw-private/game/default.xex"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_input.cmake")

add_executable(lollipop_timing_test tests/timing_test.cpp src/timing_hooks.cpp)
target_include_directories(lollipop_timing_test PRIVATE src "${LOLLIPOP_GENERATED_DIR}")
target_link_libraries(lollipop_timing_test PRIVATE rex::runtime)
add_test(NAME lollipop_timing_hooks COMMAND lollipop_timing_test "${CMAKE_CURRENT_BINARY_DIR}/tick-test.csv")

add_executable(lollipop_input_test tests/input_concurrency_test.cpp)
target_link_libraries(lollipop_input_test PRIVATE rex::runtime)
add_test(NAME lollipop_concurrent_input COMMAND lollipop_input_test)
set_tests_properties(lollipop_concurrent_input PROPERTIES TIMEOUT 30)

add_executable(lollipop_bridge_test tests/test_bridge_test.cpp src/test_bridge.cpp)
target_include_directories(lollipop_bridge_test PRIVATE src)
target_link_libraries(lollipop_bridge_test PRIVATE rex::runtime)
add_test(NAME lollipop_test_bridge COMMAND lollipop_bridge_test)

find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
add_test(NAME lollipop_frame_analysis
    COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf_analysis_test.ps1")

add_test(NAME lollipop_game_import
    COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/prepare_game_test.ps1")
