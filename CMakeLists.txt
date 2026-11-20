cmake_minimum_required(VERSION 3.22)

project(XWine
    VERSION 0.2.0
    DESCRIPTION "Experimental Windows PC gaming compatibility research environment targeting Xbox Series S Dev Mode"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

# Homebrew fallback search prefix on Apple Silicon macOS
if(APPLE AND EXISTS "/opt/homebrew")
    list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew")
endif()

find_package(SDL3 REQUIRED)
find_package(Freetype REQUIRED)
find_package(PNG REQUIRED)

function(xwine_apply_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
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

if(APPLE OR UNIX)
    list(APPEND XWINE_PLATFORM_SOURCES platform/macos/src/macos_platform.cpp)
endif()

add_library(xwine_platform STATIC ${XWINE_PLATFORM_SOURCES})
target_include_directories(xwine_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/include
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/xbox/include
)
xwine_apply_warnings(xwine_platform)

# ==============================================================================
# 2. Runtime Subsystem (xwine_runtime)
# ==============================================================================
add_library(xwine_runtime STATIC
    runtime/loader/src/pe_inspector.cpp
    runtime/memory/src/virtual_memory.cpp
    runtime/process/src/process_model.cpp
    runtime/windows-api/src/api_catalog.cpp
    runtime/filesystem/src/library_manager.cpp
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
target_link_libraries(xwine_runtime PUBLIC xwine_platform)
xwine_apply_warnings(xwine_runtime)

# ==============================================================================
# 3. Graphics Compatibility Abstraction (xwine_graphics)
# ==============================================================================
add_library(xwine_graphics STATIC
    graphics/abstraction/src/graphics_backend.cpp
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
# 4. Steam Metadata & Compatibility Subsystem (xwine_steam)
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
# 5. Controller & Keyboard Input Subsystem (xwine_input)
# ==============================================================================
add_library(xwine_input STATIC
    input/mapping/src/controller_mapping.cpp
    input/keyboard/src/keyboard_input.cpp
    input/controller/src/input_manager.cpp
)
target_include_directories(xwine_input PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/input/mapping/include
    ${CMAKE_CURRENT_SOURCE_DIR}/input/keyboard/include
    ${CMAKE_CURRENT_SOURCE_DIR}/input/controller/include
)
target_link_libraries(xwine_input PUBLIC xwine_platform SDL3::SDL3)
xwine_apply_warnings(xwine_input)

# ==============================================================================
# 6. Native Launcher Core & Executable (xwine_launcher_core + xwine-launcher)
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
# 7. Diagnostics & PE Inspection CLI Tool (xwine-diag)
# ==============================================================================
add_executable(xwine-diag
    tools/diagnostics/main.cpp
)
set_target_properties(xwine-diag PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/diagnostics"
)
target_link_libraries(xwine-diag PRIVATE
    xwine_platform
    xwine_runtime
    xwine_graphics
    xwine_steam
    xwine_input
)
xwine_apply_warnings(xwine-diag)

# ==============================================================================
# 8. Automated Test Suite (CTest)
# ==============================================================================
if(BUILD_TESTING)
    add_executable(xwine_unit_tests tests/unit/test_core_unit.cpp)
    target_link_libraries(xwine_unit_tests PRIVATE
        xwine_platform xwine_runtime xwine_graphics xwine_steam xwine_input)
    xwine_apply_warnings(xwine_unit_tests)
    add_test(NAME XWineUnitTests COMMAND xwine_unit_tests)

    add_executable(xwine_integration_tests tests/integration/test_pipeline_integration.cpp)
    target_link_libraries(xwine_integration_tests PRIVATE
        xwine_platform xwine_runtime xwine_graphics xwine_steam)
    xwine_apply_warnings(xwine_integration_tests)
    add_test(NAME XWineIntegrationTests COMMAND xwine_integration_tests)

    add_executable(xwine_compatibility_tests tests/compatibility/test_xbox_constraints.cpp)
    target_link_libraries(xwine_compatibility_tests PRIVATE
        xwine_platform xwine_runtime xwine_graphics)
    xwine_apply_warnings(xwine_compatibility_tests)
    add_test(NAME XWineCompatibilityTests COMMAND xwine_compatibility_tests)

    add_executable(xwine_launcher_tests launcher/tests/test_launcher_state.cpp)
    target_link_libraries(xwine_launcher_tests PRIVATE xwine_launcher_core)
    xwine_apply_warnings(xwine_launcher_tests)
    add_test(NAME XWineLauncherTests COMMAND xwine_launcher_tests)
endif()
