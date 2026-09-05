cmake_minimum_required(VERSION 3.21)

project(ts3-steelseries-plugin
    VERSION 0.1.0
    DESCRIPTION "TeamSpeak 3 status on the SteelSeries Arctis OLED"
    LANGUAGES CXX
)

# --------------------------------------------------------------------------
# Guard rails
#
# The TeamSpeak 3 client loads plugins in-process, so the DLL architecture
# must match the client exactly. A 32-bit build silently fails to load with
# no message in the client UI, so fail loudly here instead.
# --------------------------------------------------------------------------
if(NOT WIN32)
    message(FATAL_ERROR "This plugin targets Windows only (it uses WinHTTP and the Win32 dialog API).")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "64-bit build required to match the TeamSpeak 3 client. Configure with -A x64.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TS3SS_BUILD_TESTS "Build unit tests for the pure-logic layers" ON)

# --------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------
set(TS3_SDK_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ts3-pluginsdk/include")
if(NOT EXISTS "${TS3_SDK_INCLUDE}/teamspeak/public_definitions.h")
    message(FATAL_ERROR
        "TeamSpeak plugin SDK headers not found.\n"
        "Run: git submodule update --init --recursive")
endif()

include(FetchContent)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# --------------------------------------------------------------------------
# Plugin target
#
# Sources are globbed on purpose: this project grows one file per widget, and
# a glob keeps "add a feature" down to dropping in a .cpp. Re-run CMake after
# adding files (CONFIGURE_DEPENDS does this automatically for the VS and
# Ninja generators).
# --------------------------------------------------------------------------
file(GLOB_RECURSE TS3SS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

if(NOT TS3SS_SOURCES)
    message(STATUS "")
    message(STATUS "  No sources under src/ yet - configuring the scaffold only.")
    message(STATUS "  See docs/roadmap.md for the implementation phases.")
    message(STATUS "")
    return()
endif()

# The resource script (version info + configuration dialog template) arrives in
# phase 1/4; until then the target builds without it.
set(TS3SS_RESOURCES "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/plugin.rc")
    list(APPEND TS3SS_RESOURCES resources/plugin.rc)
endif()

add_library(ts3_steelseries SHARED ${TS3SS_SOURCES} ${TS3SS_RESOURCES})

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
    @ONLY
)

target_include_directories(ts3_steelseries PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources"   # resource.h, shared with plugin.rc
    "${CMAKE_BINARY_DIR}/generated"
    "${TS3_SDK_INCLUDE}"
)

target_link_libraries(ts3_steelseries PRIVATE
    nlohmann_json::nlohmann_json
    winhttp      # GameSense HTTP transport
    shlwapi      # path handling for coreProps.json / config discovery
    comctl32     # configuration dialog
)

target_compile_definitions(ts3_steelseries PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_compile_options(ts3_steelseries PRIVATE /W4 /permissive- /utf-8 /EHsc)

# TeamSpeak resolves plugins by file name; keep it stable across configurations.
set_target_properties(ts3_steelseries PROPERTIES
    OUTPUT_NAME "ts3_steelseries"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --------------------------------------------------------------------------
# Tests
#
# Only the pure layers (state, composer, bitmap, config, JSON builders) are
# tested. Anything touching the TS3 ABI or a live GameSense socket is not.
# --------------------------------------------------------------------------
if(TS3SS_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
