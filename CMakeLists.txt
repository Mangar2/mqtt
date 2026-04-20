cmake_minimum_required(VERSION 3.16)

project(mqtt-broker
    VERSION 0.1.0
    DESCRIPTION "MQTT 5.0 Broker"
    LANGUAGES CXX
)

#  Language standard 
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#  Options 
option(MQTT_SANITIZE "Enable AddressSanitizer and UBSan (host builds only)" OFF)
option(MQTT_COVERAGE "Enable clang source-based coverage instrumentation"   OFF)
option(MQTT_TRACING_DISABLED "Compile out structured tracing guards" OFF)

#  Shared compiler/linker flags 
set(MQTT_COMPILE_OPTIONS
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Release>:-O3>
    $<$<BOOL:${MQTT_SANITIZE}>:-fsanitize=address>
    $<$<BOOL:${MQTT_SANITIZE}>:-fsanitize=undefined>
    $<$<BOOL:${MQTT_COVERAGE}>:-fprofile-instr-generate>
    $<$<BOOL:${MQTT_COVERAGE}>:-fcoverage-mapping>
)

set(MQTT_LINK_OPTIONS
    $<$<BOOL:${MQTT_SANITIZE}>:-fsanitize=address>
    $<$<BOOL:${MQTT_SANITIZE}>:-fsanitize=undefined>
    $<$<BOOL:${MQTT_COVERAGE}>:-fprofile-instr-generate>
)

#  Source discovery 
# Collect all production .cpp files, then exclude test directories and the
# platform-specific files for the OS we are NOT building on.
file(GLOB_RECURSE MQTT_ALL_SOURCES  CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE MQTT_TEST_SOURCES CONFIGURE_DEPENDS "src/*_test.cpp")
list(FILTER MQTT_ALL_SOURCES EXCLUDE REGEX "/test/")

if(WIN32)
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "_posix\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "_posix\\.cpp$")
else()
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "_win32\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "_win32\\.cpp$")
endif()

if(APPLE OR CMAKE_SYSTEM_NAME MATCHES ".*BSD")
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "_epoll\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "_epoll\\.cpp$")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "_kqueue\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "_kqueue\\.cpp$")
else()
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "_kqueue\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "_kqueue\\.cpp$")
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "_epoll\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "_epoll\\.cpp$")
endif()

#  Main executable 
add_executable(mqtt-broker ${MQTT_ALL_SOURCES})

target_compile_options(mqtt-broker PRIVATE ${MQTT_COMPILE_OPTIONS})
target_link_options(mqtt-broker    PRIVATE ${MQTT_LINK_OPTIONS})
target_include_directories(mqtt-broker PRIVATE src)
target_compile_definitions(mqtt-broker PRIVATE
    $<$<BOOL:${MQTT_TRACING_DISABLED}>:MQTT_TRACING_DISABLED>)
if(WIN32)
    target_link_libraries(mqtt-broker PRIVATE ws2_32)
endif()

#  Unit tests 
include(CTest)

if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    # Placeholder keeps the test target valid before any real tests exist.
    file(GLOB_RECURSE MQTT_PLACEHOLDER CONFIGURE_DEPENDS "tests/unit/*.cpp")

    # Exclude main.cpp from the test binary (it defines main; Catch2 provides its own).
    set(MQTT_LIB_SOURCES ${MQTT_ALL_SOURCES})
    list(FILTER MQTT_LIB_SOURCES EXCLUDE REGEX "/main\\.cpp$")

    add_executable(mqtt-broker-tests
        ${MQTT_PLACEHOLDER}
        ${MQTT_TEST_SOURCES}
        ${MQTT_LIB_SOURCES}
    )

    target_compile_options(mqtt-broker-tests PRIVATE ${MQTT_COMPILE_OPTIONS})
    target_link_options(mqtt-broker-tests    PRIVATE ${MQTT_LINK_OPTIONS})
    target_compile_definitions(mqtt-broker-tests PRIVATE
        $<$<BOOL:${MQTT_TRACING_DISABLED}>:MQTT_TRACING_DISABLED>)
    target_link_libraries(mqtt-broker-tests  PRIVATE Catch2::Catch2WithMain)
    target_include_directories(mqtt-broker-tests PRIVATE src)
    if(WIN32)
        target_link_libraries(mqtt-broker-tests PRIVATE ws2_32)
    endif()

    include(Catch)
    catch_discover_tests(mqtt-broker-tests)
endif()

#  Install 
include(GNUInstallDirs)
install(TARGETS mqtt-broker
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
