# Only the TS3-free layers are tested here: ClientState, composer, widgets, config
# parsing. Anything needing ts3_functions.h or a live GameSense socket does not belong
# in unit tests - that is what the phase 0 probes and the manual runs in
# docs/roadmap.md are for.

# Look for test sources BEFORE pulling in a test framework. Downloading doctest to
# then discover there is nothing to test wastes a configure step and, worse, makes an
# unrelated third-party CMake incompatibility fail the whole project build.
file(GLOB_RECURSE TS3SS_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
if(NOT TS3SS_TEST_SOURCES)
    message(STATUS "  No tests yet - see docs/roadmap.md phase 3.")
    return()
endif()

include(FetchContent)

# doctest 2.4.11 still declares cmake_minimum_required(VERSION 3.0), which CMake 4.x
# refuses outright. This tells CMake to treat that project as if it had asked for 3.5.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(doctest)

# Everything except the ABI layer, which cannot be meaningfully instantiated without a
# running TeamSpeak client.
set(TS3SS_TESTABLE ${TS3SS_SOURCES})
list(FILTER TS3SS_TESTABLE EXCLUDE REGEX "/src/plugin/")

add_executable(ts3ss_tests ${TS3SS_TEST_SOURCES} ${TS3SS_TESTABLE})
target_include_directories(ts3ss_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
)
target_link_libraries(ts3ss_tests PRIVATE doctest::doctest nlohmann_json::nlohmann_json winhttp)
# /wd5285 silences a warning from doctest's own header (it specialises std::tuple).
# Scoped to the test target so our own code keeps a clean /W4.
target_compile_options(ts3ss_tests PRIVATE /W4 /permissive- /utf-8 /EHsc /wd5285)
target_compile_definitions(ts3ss_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

# Registered as one CTest entry rather than via doctest_discover_tests, which would
# need doctest's own module path wired into CMAKE_MODULE_PATH. The binary reports each
# failing case by name anyway, so per-case CTest entries buy nothing here.
add_test(NAME ts3ss_tests COMMAND ts3ss_tests)
