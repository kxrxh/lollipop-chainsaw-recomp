cmake_minimum_required(VERSION 3.25)
project(lollipop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(rexglue 0.11.0 EXACT REQUIRED CONFIG)

set(LOLLIPOP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/lollipop_manifest.toml"
    CACHE FILEPATH "Code-generation manifest")
set(LOLLIPOP_GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/game"
    CACHE PATH "Extracted supported game data")
set(LOLLIPOP_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/out/generated"
    CACHE PATH "Local ReXGlue codegen output")
if(NOT EXISTS "${LOLLIPOP_GENERATED_DIR}/sources.cmake")
    message(FATAL_ERROR "Run rexglue codegen lollipop_manifest.toml first, or use build.ps1")
endif()
include("${LOLLIPOP_GENERATED_DIR}/sources.cmake")

add_custom_target(lollipop_codegen
    COMMAND $<TARGET_FILE:rex::rexglue> codegen "${LOLLIPOP_MANIFEST}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)

add_library(lollipop_recomp OBJECT ${GENERATED_SOURCES})
target_include_directories(lollipop_recomp PRIVATE "${LOLLIPOP_GENERATED_DIR}")
target_link_libraries(lollipop_recomp PRIVATE rex::sdk)
rexglue_recomp_options(recomp_options optimized)
target_compile_options(lollipop_recomp PRIVATE ${recomp_options})
target_precompile_headers(lollipop_recomp PRIVATE "${LOLLIPOP_GENERATED_DIR}/lollipop_pch.h")
rexglue_apply_target_settings(lollipop_recomp)

add_library(lollipop_host_sdk STATIC src/settings/runtime_settings.cpp)
target_include_directories(lollipop_host_sdk PUBLIC src)
target_link_libraries(lollipop_host_sdk PUBLIC rex::sdk rex::ui rex::filesystem rex::gpu rex::hid)

add_executable(lollipop WIN32
    src/main.cpp
    src/app/application.cpp
    src/game/game_settings.cpp
    src/game/cheats.cpp
    src/game/combat_timing.cpp
    src/game/game_bridge.cpp
    src/game/game_menus.cpp
    src/game/native_quit_dialog.cpp
    src/game/main_menu.cpp
    src/game/package_lzx.cpp
    src/game/shader_inventory.cpp
    src/game/shadow_settings.cpp
    src/game/startup_hooks.cpp
    src/game/timing_hooks.cpp
    src/input/keyboard_controls.cpp
    src/input/prompt_artwork.cpp
    src/input/live_prompts.cpp
    src/settings/pc_settings.cpp
    src/testing/test_bridge.cpp
    src/ui/settings_menu.cpp
    src/ui/cheats_menu.cpp
    src/ui/exit_flow.cpp
    src/ui/shader_loading.cpp)
if(WIN32)
    enable_language(RC)
    target_sources(lollipop PRIVATE assets/lollipop.rc)
    set_property(SOURCE assets/lollipop.rc APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/launcher/assets/lollipop.ico"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/settings/panel.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/settings/controls.png")
    set_property(SOURCE assets/lollipop.rc APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/settings/quit-game.png")
    set_property(SOURCE assets/lollipop.rc APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/pipelines.txt")
    target_include_directories(lollipop PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()
target_include_directories(lollipop PRIVATE src "${LOLLIPOP_GENERATED_DIR}")
target_link_libraries(lollipop PRIVATE lollipop_recomp lollipop_host_sdk gdiplus bcrypt)
rexglue_configure_target(lollipop)
# Relink and restage if a runtime-loaded SDK module changes between builds.
get_property(sdk_targets DIRECTORY PROPERTY IMPORTED_TARGETS)
foreach(sdk_target IN LISTS sdk_targets)
    get_target_property(sdk_type ${sdk_target} TYPE)
    if(sdk_type STREQUAL "SHARED_LIBRARY" AND sdk_target MATCHES "^(rex|SDL3)::")
        set_property(TARGET lollipop APPEND PROPERTY LINK_DEPENDS "$<TARGET_FILE:${sdk_target}>")
    endif()
endforeach()
add_custom_command(TARGET lollipop POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E touch "$<TARGET_FILE_DIR:lollipop>/shader-precompile-v1.ready"
    VERBATIM)

include(CTest)
if(NOT BUILD_TESTING)
    return()
endif()

add_executable(lollipop_shader_tool src/app/shader_tool.cpp src/game/shader_inventory.cpp
    src/game/package_lzx.cpp)
target_include_directories(lollipop_shader_tool PRIVATE src)
target_link_libraries(lollipop_shader_tool PRIVATE lollipop_host_sdk)
add_executable(lollipop_shader_inventory_test tests/shader_inventory_test.cpp
    src/game/shader_inventory.cpp src/game/package_lzx.cpp)
target_include_directories(lollipop_shader_inventory_test PRIVATE src)
target_link_libraries(lollipop_shader_inventory_test PRIVATE lollipop_host_sdk)
add_test(NAME lollipop_shader_inventory COMMAND lollipop_shader_inventory_test
    "${CMAKE_CURRENT_BINARY_DIR}/shader-inventory-test")
set_tests_properties(lollipop_shader_inventory PROPERTIES TIMEOUT 30)

add_executable(lollipop_copy_export_test tests/copy_export_test.cpp)
target_link_libraries(lollipop_copy_export_test PRIVATE lollipop_host_sdk)
add_test(NAME lollipop_checkpoint_copy_export COMMAND lollipop_copy_export_test)

add_executable(lollipop_imgui_repaint_test tests/imgui_repaint_test.cpp)
target_link_libraries(lollipop_imgui_repaint_test PRIVATE lollipop_host_sdk)
add_test(NAME lollipop_imgui_repaint COMMAND lollipop_imgui_repaint_test)

set(LOLLIPOP_HEADERS
    app/application.h game/game_settings.h game/game_bridge.h game/game_menus.h game/cheats.h
    game/native_quit_dialog.h game/main_menu.h game/package_lzx.h
    game/startup_hooks.h game/timing_hooks.h game/shadow_settings.h
    game/shader_inventory.h ui/shader_loading.h
    input/keyboard_controls.h input/live_prompts.h input/prompt_artwork.h
    settings/pc_settings.h settings/runtime_settings.h testing/test_bridge.h ui/settings_menu.h ui/exit_flow.h ui/cheats_menu.h)
set(header_checks)
foreach(header IN LISTS LOLLIPOP_HEADERS)
    string(REPLACE "/" "_" check_name "${header}")
    set(check_file "${CMAKE_CURRENT_BINARY_DIR}/header-check/${check_name}.cpp")
    file(GENERATE OUTPUT "${check_file}" CONTENT "#include \"${header}\"\n")
    list(APPEND header_checks "${check_file}")
endforeach()
add_library(lollipop_header_check OBJECT ${header_checks})
target_include_directories(lollipop_header_check PRIVATE src)
target_link_libraries(lollipop_header_check PRIVATE lollipop_host_sdk)

add_executable(lollipop_settings_test tests/pc_settings_test.cpp src/settings/pc_settings.cpp)
target_include_directories(lollipop_settings_test PRIVATE src)
target_link_libraries(lollipop_settings_test PRIVATE lollipop_host_sdk)
add_test(NAME lollipop_pc_settings COMMAND lollipop_settings_test
    "${CMAKE_CURRENT_BINARY_DIR}/settings-test/pc-settings.toml")
add_test(NAME lollipop_settings_migration COMMAND lollipop_settings_test
    "${CMAKE_CURRENT_BINARY_DIR}/settings-test/legacy-settings.toml" --legacy)
add_test(NAME lollipop_settings_legacy_60fps COMMAND lollipop_settings_test
    "${CMAKE_CURRENT_BINARY_DIR}/settings-test/legacy-60fps.toml" --legacy-60)
add_test(NAME lollipop_settings_fps_precedence COMMAND lollipop_settings_test
    "${CMAKE_CURRENT_BINARY_DIR}/settings-test/fps-precedence.toml" --fps-precedence)
add_test(NAME lollipop_settings_duplicates COMMAND lollipop_settings_test
    "${CMAKE_CURRENT_BINARY_DIR}/settings-test/duplicate-settings.toml" --duplicate)
add_test(NAME lollipop_settings_shader_default COMMAND lollipop_settings_test
    "${CMAKE_CURRENT_BINARY_DIR}/settings-test/shader-default.toml" --shader-default)
add_test(NAME lollipop_settings_shader_cli COMMAND lollipop_settings_test
    "${CMAKE_CURRENT_BINARY_DIR}/settings-test/shader-cli.toml" --shader-cli)

add_executable(lollipop_shadow_test tests/shadow_settings_test.cpp src/game/shadow_settings.cpp)
target_include_directories(lollipop_shadow_test PRIVATE src)
target_link_libraries(lollipop_shadow_test PRIVATE lollipop_host_sdk)
add_test(NAME lollipop_shadow_settings COMMAND lollipop_shadow_test)

add_executable(lollipop_keyboard_test tests/keyboard_controls_test.cpp
    src/input/keyboard_controls.cpp src/input/prompt_artwork.cpp
    src/game/package_lzx.cpp src/game/main_menu.cpp)
target_include_directories(lollipop_keyboard_test PRIVATE src assets)
target_link_libraries(lollipop_keyboard_test PRIVATE lollipop_host_sdk gdiplus bcrypt)
add_test(NAME lollipop_keyboard_controls COMMAND lollipop_keyboard_test
    "${LOLLIPOP_GAME_DIR}/AshGame/CookedXbox360/Startup.xxx"
    "${CMAKE_CURRENT_BINARY_DIR}/keyboard-startup.xxx")
add_test(NAME lollipop_input_identity
    COMMAND "${CMAKE_COMMAND}"
        "-DGAME_XEX=${LOLLIPOP_GAME_DIR}/default.xex"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_input.cmake")

add_executable(lollipop_main_menu_test tests/main_menu_test.cpp src/game/main_menu.cpp
    src/game/package_lzx.cpp)
target_include_directories(lollipop_main_menu_test PRIVATE src assets)
target_link_libraries(lollipop_main_menu_test PRIVATE lollipop_host_sdk bcrypt)
add_test(NAME lollipop_main_menu COMMAND lollipop_main_menu_test
    "${LOLLIPOP_GAME_DIR}/AshGame/CookedXbox360/MENU_PL.xxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/settings/quit-game.png")

add_executable(lollipop_timing_test tests/timing_test.cpp src/game/timing_hooks.cpp)
target_include_directories(lollipop_timing_test PRIVATE src)
target_link_libraries(lollipop_timing_test PRIVATE lollipop_host_sdk)
add_test(NAME lollipop_timing_hooks COMMAND lollipop_timing_test "${CMAKE_CURRENT_BINARY_DIR}/tick-test.csv")
add_test(NAME lollipop_timing_legacy_cli COMMAND lollipop_timing_test --legacy-cli)
add_test(NAME lollipop_timing_fps_cli COMMAND lollipop_timing_test --fps-cli)

add_executable(lollipop_combat_timing_test tests/combat_timing_test.cpp
    src/game/combat_timing.cpp src/game/timing_hooks.cpp)
target_include_directories(lollipop_combat_timing_test PRIVATE src)
target_link_libraries(lollipop_combat_timing_test PRIVATE lollipop_host_sdk)
add_test(NAME lollipop_combat_timing COMMAND lollipop_combat_timing_test)

add_executable(lollipop_input_test tests/input_concurrency_test.cpp)
target_link_libraries(lollipop_input_test PRIVATE lollipop_host_sdk)
add_test(NAME lollipop_concurrent_input COMMAND lollipop_input_test)
set_tests_properties(lollipop_concurrent_input PROPERTIES TIMEOUT 30)

add_executable(lollipop_exit_flow_test tests/exit_flow_test.cpp src/ui/exit_flow.cpp)
target_include_directories(lollipop_exit_flow_test PRIVATE src)
target_link_libraries(lollipop_exit_flow_test PRIVATE lollipop_host_sdk)
add_test(NAME lollipop_exit_flow COMMAND lollipop_exit_flow_test)

add_executable(lollipop_bridge_test tests/test_bridge_test.cpp
    src/testing/test_bridge.cpp src/game/game_settings.cpp src/game/game_bridge.cpp
    src/game/native_quit_dialog.cpp src/game/startup_hooks.cpp src/game/timing_hooks.cpp
    src/game/cheats.cpp)
target_include_directories(lollipop_bridge_test PRIVATE src)
target_link_libraries(lollipop_bridge_test PRIVATE lollipop_host_sdk)
add_test(NAME lollipop_test_bridge COMMAND lollipop_bridge_test)

add_executable(lollipop_cheats_test tests/cheats_test.cpp src/game/cheats.cpp)
target_include_directories(lollipop_cheats_test PRIVATE src)
target_link_libraries(lollipop_cheats_test PRIVATE lollipop_host_sdk)
add_test(NAME lollipop_cheats COMMAND lollipop_cheats_test)

add_executable(lollipop_cheats_menu_test tests/cheats_menu_test.cpp
    src/ui/cheats_menu.cpp src/game/cheats.cpp src/game/game_bridge.cpp
    src/game/game_settings.cpp src/game/native_quit_dialog.cpp src/game/startup_hooks.cpp
    src/game/timing_hooks.cpp src/testing/test_bridge.cpp)
target_include_directories(lollipop_cheats_menu_test PRIVATE src)
target_link_libraries(lollipop_cheats_menu_test PRIVATE lollipop_host_sdk)
add_test(NAME lollipop_cheats_menu COMMAND lollipop_cheats_menu_test)

add_executable(lollipop_startup_test tests/startup_hooks_test.cpp src/game/startup_hooks.cpp)
target_include_directories(lollipop_startup_test PRIVATE src)
target_link_libraries(lollipop_startup_test PRIVATE lollipop_host_sdk)
add_test(NAME lollipop_offline_startup COMMAND lollipop_startup_test)

# Every executable exchanging C++ objects with SDK modules must use the SDK allocator.
get_property(lollipop_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS lollipop_targets)
    if(target MATCHES "^lollipop_.*(_test|_tool)$")
        rexglue_link_host_allocator(${target})
    endif()
endforeach()

option(LOLLIPOP_SCRIPT_TESTS "Run development PowerShell script checks" ON)
if(LOLLIPOP_SCRIPT_TESTS)
    find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
    get_filename_component(lollipop_compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
    add_test(NAME lollipop_frame_analysis
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf_analysis_test.ps1")
    add_test(NAME lollipop_game_import
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/prepare_game_test.ps1")
    add_test(NAME lollipop_project_paths
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/project_paths_test.ps1"
            -RexGlueDirectory "${rexglue_DIR}"
            -GeneratedDirectory "${LOLLIPOP_GENERATED_DIR}"
            -LlvmDirectory "${lollipop_compiler_directory}")
endif()

if(WIN32)
    find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
    file(GLOB_RECURSE setup_sources CONFIGURE_DEPENDS
        launcher/*.cs launcher/*.xaml launcher/*.csproj launcher/*.props
        launcher/*.json launcher/*.ps1 launcher/assets/*)
    set(setup_test "${CMAKE_CURRENT_BINARY_DIR}/lollipop_setup_test.exe")
    add_custom_command(OUTPUT "${setup_test}"
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File
            "${CMAKE_CURRENT_SOURCE_DIR}/launcher/build.ps1" -Tests -OutputFile "${setup_test}"
        DEPENDS ${setup_sources} launcher/VERSION VERSION
        VERBATIM)
    add_custom_target(lollipop_setup_check ALL DEPENDS "${setup_test}")
    add_dependencies(lollipop_setup_check lollipop_settings_test)
    add_test(NAME lollipop_standalone_setup COMMAND "${setup_test}" "${CMAKE_CURRENT_SOURCE_DIR}"
        "$<TARGET_FILE:lollipop_settings_test>" "${CMAKE_COMMAND}" "${CMAKE_MAKE_PROGRAM}"
        "${CMAKE_CXX_COMPILER}")
    set_tests_properties(lollipop_standalone_setup PROPERTIES TIMEOUT 120)
endif()
