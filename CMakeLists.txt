cmake_minimum_required(VERSION 3.22)

project(XWine
    VERSION 0.3.0
    DESCRIPTION "Experimental Windows PC gaming compatibility research environment targeting Xbox Series S Dev Mode"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(XWINE_TARGET_XBOX_UWP "Build with Xbox Series S Developer Mode (UWP / OneCoreUAP) platform backend" OFF)

include(CTest)

if(APPLE AND EXISTS "/opt/homebrew")
    list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew")
endif()

find_package(SDL3 REQUIRED)
find_package(Freetype REQUIRED)
find_package(PNG REQUIRED)

function(xwine_apply_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /utf-8)
        target_compile_definitions(${target_name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ==============================================================================
# 1. Platform Abstraction Library (xwine_platform)
# ==============================================================================
set(XWINE_PLATFORM_SOURCES
    platform/common/src/logger.cpp
    platform/common/src/json.cpp
    platform/common/src/config.cpp
    platform/common/src/benchmark.cpp
    platform/xbox/src/xbox_platform.cpp
)

if(WIN32 OR XWINE_TARGET_XBOX_UWP)
    list(APPEND XWINE_PLATFORM_SOURCES platform/xbox/src/xbox_uwp_platform.cpp)
else()
    list(APPEND XWINE_PLATFORM_SOURCES platform/macos/src/macos_platform.cpp)
endif()

add_library(xwine_platform STATIC ${XWINE_PLATFORM_SOURCES})
target_include_directories(xwine_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/include
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/xbox/include
)
if(XWINE_TARGET_XBOX_UWP)
    target_compile_definitions(xwine_platform PUBLIC XWINE_PLATFORM_XBOX=1)
endif()
if(WIN32)
    if(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
        target_link_libraries(xwine_platform PUBLIC WindowsApp)
    else()
        # OneCoreUAP exposes VirtualAllocFromApp & VirtualProtectFromApp on Windows 10/11 & Xbox
        target_link_libraries(xwine_platform PUBLIC kernel32 onecoreuap)
    endif()
endif()
xwine_apply_warnings(xwine_platform)

# ==============================================================================
# 2. Controller & Input Mapping Core (shared by Runtime XInput shims & Input)
# ==============================================================================
add_library(xwine_input_mapping STATIC
    input/mapping/src/controller_mapping.cpp
)
target_include_directories(xwine_input_mapping PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/input/mapping/include
)
target_link_libraries(xwine_input_mapping PUBLIC xwine_platform)
xwine_apply_warnings(xwine_input_mapping)

# ==============================================================================
# 3. Runtime Subsystem (xwine_runtime — Phases 3 & 4)
# ==============================================================================
add_library(xwine_runtime STATIC
    runtime/loader/src/pe_inspector.cpp
    runtime/loader/src/pe_loader.cpp
    runtime/memory/src/virtual_memory.cpp
    runtime/process/src/process_model.cpp
    runtime/windows-api/src/api_catalog.cpp
    runtime/windows-api/src/win32_shims.cpp
    runtime/filesystem/src/library_manager.cpp
    runtime/filesystem/src/virtual_registry.cpp
    runtime/filesystem/src/path_virtualizer.cpp
    runtime/core/src/runtime_manager.cpp
)
target_include_directories(xwine_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/loader/include
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/memory/include
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/process/include
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/windows-api/include
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/filesystem/include
)
target_link_libraries(xwine_runtime PUBLIC xwine_platform xwine_input_mapping)
xwine_apply_warnings(xwine_runtime)

# ==============================================================================
# 4. Graphics Compatibility Abstraction & Swapchain Bridge (xwine_graphics — Phase 5)
# ==============================================================================
add_library(xwine_graphics STATIC
    graphics/abstraction/src/graphics_backend.cpp
    graphics/abstraction/src/swapchain_bridge.cpp
    graphics/d3d9/d3d9_translator.cpp
    graphics/d3d10/d3d10_translator.cpp
    graphics/d3d11/d3d11_translator.cpp
    graphics/d3d12/d3d12_translator.cpp
    graphics/vulkan/vulkan_assessment.cpp
    graphics/shaders/shader_pipeline.cpp
)
target_include_directories(xwine_graphics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/graphics/abstraction/include
)
target_link_libraries(xwine_graphics PUBLIC xwine_platform xwine_runtime)
xwine_apply_warnings(xwine_graphics)

# ==============================================================================
# 5. Steam Metadata & Compatibility Subsystem (xwine_steam — Phase 8)
# ==============================================================================
add_library(xwine_steam STATIC
    steam/metadata/src/vdf_parser.cpp
    steam/compatibility/src/steam_compatibility.cpp
    steam/library/src/steam_library.cpp
)
target_include_directories(xwine_steam PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/steam/metadata/include
    ${CMAKE_CURRENT_SOURCE_DIR}/steam/compatibility/include
    ${CMAKE_CURRENT_SOURCE_DIR}/steam/library/include
)
target_link_libraries(xwine_steam PUBLIC xwine_platform xwine_runtime)
xwine_apply_warnings(xwine_steam)

# ==============================================================================
# 6. SDL3 Controller & Keyboard Input Subsystem (xwine_input)
# ==============================================================================
add_library(xwine_input STATIC
    input/keyboard/src/keyboard_input.cpp
    input/controller/src/input_manager.cpp
)
target_include_directories(xwine_input PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/input/keyboard/include
    ${CMAKE_CURRENT_SOURCE_DIR}/input/controller/include
)
target_link_libraries(xwine_input PUBLIC xwine_input_mapping SDL3::SDL3)
xwine_apply_warnings(xwine_input)

# ==============================================================================
# 7. Native Launcher Core & Executable (xwine_launcher_core + xwine-launcher)
# ==============================================================================
add_library(xwine_launcher_core STATIC
    launcher/src/launcher_state.cpp
)
target_include_directories(xwine_launcher_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/launcher/include
)
target_link_libraries(xwine_launcher_core PUBLIC
    xwine_platform
    xwine_runtime
    xwine_graphics
    xwine_steam
    xwine_input
)
xwine_apply_warnings(xwine_launcher_core)

add_executable(xwine-launcher
    launcher/ui/ui_renderer.cpp
    launcher/src/main.cpp
)
set_target_properties(xwine-launcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/launcher"
)
target_link_libraries(xwine-launcher PRIVATE
    xwine_launcher_core
    SDL3::SDL3
    Freetype::Freetype
    PNG::PNG
)
xwine_apply_warnings(xwine-launcher)

# ==============================================================================
# 8. CLI Tools: Diagnostics (`xwine-diag`), Phase 5 Graphics (`xwine-gfx-test`),
#    and Phase 7 Original Windows x64 Test Probe (`xwine-phase7-probe`)
# ==============================================================================
add_executable(xwine-diag
    tools/diagnostics/main.cpp
)
set_target_properties(xwine-diag PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/diagnostics"
)
target_link_libraries(xwine-diag PRIVATE
    xwine_graphics
    xwine_steam
    xwine_input
)
xwine_apply_warnings(xwine-diag)

add_executable(xwine-gfx-test
    tools/graphics-bench/main.cpp
)
set_target_properties(xwine-gfx-test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/graphics-bench"
)
target_link_libraries(xwine-gfx-test PRIVATE
    xwine_graphics
    SDL3::SDL3
)
xwine_apply_warnings(xwine-gfx-test)

if(WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    add_executable(xwine-phase7-probe WIN32
        tools/phase7-probe/xwine_probe.c
    )
    set_target_properties(xwine-phase7-probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/phase7-probe"
    )
    if(MSVC)
        target_compile_options(xwine-phase7-probe PRIVATE /GS-)
        target_link_options(xwine-phase7-probe PRIVATE /NODEFAULTLIB /ENTRY:XWineProbeMain)
    endif()
    target_link_libraries(xwine-phase7-probe PRIVATE kernel32 user32 advapi32 xinput)
endif()

# ==============================================================================
# 9. Automated Test Suites (CTest)
# ==============================================================================
if(BUILD_TESTING)
    add_executable(xwine_unit_tests tests/unit/test_core_unit.cpp)
    target_link_libraries(xwine_unit_tests PRIVATE xwine_graphics xwine_steam xwine_input)
    xwine_apply_warnings(xwine_unit_tests)
    add_test(NAME XWineUnitTests COMMAND xwine_unit_tests)

    add_executable(xwine_runtime_shims_tests tests/unit/test_runtime_shims.cpp)
    target_link_libraries(xwine_runtime_shims_tests PRIVATE xwine_runtime)
    xwine_apply_warnings(xwine_runtime_shims_tests)
    add_test(NAME XWineRuntimeShimsTests COMMAND xwine_runtime_shims_tests)

    add_executable(xwine_graphics_tests tests/unit/test_graphics_bridge.cpp)
    target_link_libraries(xwine_graphics_tests PRIVATE xwine_graphics)
    xwine_apply_warnings(xwine_graphics_tests)
    add_test(NAME XWineGraphicsTests COMMAND xwine_graphics_tests)

    add_executable(xwine_integration_tests tests/integration/test_pipeline_integration.cpp)
    target_link_libraries(xwine_integration_tests PRIVATE xwine_graphics xwine_steam)
    xwine_apply_warnings(xwine_integration_tests)
    add_test(NAME XWineIntegrationTests COMMAND xwine_integration_tests)

    add_executable(xwine_compatibility_tests tests/compatibility/test_xbox_constraints.cpp)
    target_link_libraries(xwine_compatibility_tests PRIVATE xwine_graphics)
    xwine_apply_warnings(xwine_compatibility_tests)
    add_test(NAME XWineCompatibilityTests COMMAND xwine_compatibility_tests)

    add_executable(xwine_launcher_tests launcher/tests/test_launcher_state.cpp)
    target_link_libraries(xwine_launcher_tests PRIVATE xwine_launcher_core)
    xwine_apply_warnings(xwine_launcher_tests)
    add_test(NAME XWineLauncherTests COMMAND xwine_launcher_tests)
endif()
