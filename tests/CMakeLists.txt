# Getestet werden ausschliesslich die TS3-freien Schichten: ClientState, Composer,
# Widgets, Config-Parsing und die Bitmap-Packung. Alles, was ts3_functions.h oder
# eine echte GameSense-Verbindung braucht, gehoert nicht hierher -- das prueft
# Phase 0 bzw. der manuelle Durchlauf aus docs/roadmap.md.

include(FetchContent)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(doctest)

file(GLOB_RECURSE TS3SS_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
if(NOT TS3SS_TEST_SOURCES)
    message(STATUS "  No tests yet - see docs/roadmap.md phase 3.")
    return()
endif()

# Die zu testenden Quellen: alles ausser der ABI-Schicht (src/plugin/), die ohne
# einen laufenden TeamSpeak-Client nicht sinnvoll instanziierbar ist.
set(TS3SS_TESTABLE ${TS3SS_SOURCES})
list(FILTER TS3SS_TESTABLE EXCLUDE REGEX "/src/plugin/")

add_executable(ts3ss_tests ${TS3SS_TEST_SOURCES} ${TS3SS_TESTABLE})
target_include_directories(ts3ss_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(ts3ss_tests PRIVATE doctest::doctest nlohmann_json::nlohmann_json)
target_compile_options(ts3ss_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_compile_definitions(ts3ss_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

include(doctest)
doctest_discover_tests(ts3ss_tests)
