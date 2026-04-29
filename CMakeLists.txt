cmake_minimum_required(VERSION 3.16)

project(yahabroker
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
list(FILTER MQTT_ALL_SOURCES EXCLUDE REGEX "/test_client_main\\.cpp$")
list(FILTER MQTT_ALL_SOURCES EXCLUDE REGEX "/yaha_msgstoreclient_main\\.cpp$")

if(WIN32)
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "_posix\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "_posix\\.cpp$")
else()
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "_win32\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "_win32\\.cpp$")
endif()

if(APPLE OR CMAKE_SYSTEM_NAME MATCHES ".*BSD")
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "io_reactor_unsupported\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "io_reactor_unsupported\\.cpp$")
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "io_reactor_win32\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "io_reactor_win32\\.cpp$")
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "_epoll\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "_epoll\\.cpp$")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "io_reactor_unsupported\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "io_reactor_unsupported\\.cpp$")
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "io_reactor_win32\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "io_reactor_win32\\.cpp$")
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "_kqueue\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "_kqueue\\.cpp$")
elseif(WIN32)
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "io_reactor_unsupported\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "io_reactor_unsupported\\.cpp$")
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "_kqueue\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "_kqueue\\.cpp$")
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "_epoll\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "_epoll\\.cpp$")
else()
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "io_reactor_win32\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "io_reactor_win32\\.cpp$")
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "_kqueue\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "_kqueue\\.cpp$")
    list(FILTER MQTT_ALL_SOURCES  EXCLUDE REGEX "_epoll\\.cpp$")
    list(FILTER MQTT_TEST_SOURCES EXCLUDE REGEX "_epoll\\.cpp$")
endif()

#  Main executable 
add_executable(yahabroker ${MQTT_ALL_SOURCES})

target_compile_options(yahabroker PRIVATE ${MQTT_COMPILE_OPTIONS})
target_link_options(yahabroker    PRIVATE ${MQTT_LINK_OPTIONS})
target_include_directories(yahabroker PRIVATE src)
target_include_directories(yahabroker PRIVATE ${httplib_SOURCE_DIR})
target_compile_definitions(yahabroker PRIVATE
    $<$<BOOL:${MQTT_TRACING_DISABLED}>:MQTT_TRACING_DISABLED>)
if(WIN32)
    target_link_libraries(yahabroker PRIVATE ws2_32)
endif()

#  Step 27 test-client shell executable
set(MQTT_LIBRARY_SOURCES ${MQTT_ALL_SOURCES})
list(FILTER MQTT_LIBRARY_SOURCES EXCLUDE REGEX "/main\\.cpp$")

add_executable(yahatestclient
    src/test_client_main.cpp
    ${MQTT_LIBRARY_SOURCES}
)

target_compile_options(yahatestclient PRIVATE ${MQTT_COMPILE_OPTIONS})
target_link_options(yahatestclient    PRIVATE ${MQTT_LINK_OPTIONS})
target_include_directories(yahatestclient PRIVATE src)
target_include_directories(yahatestclient PRIVATE ${httplib_SOURCE_DIR})
target_compile_definitions(yahatestclient PRIVATE
    $<$<BOOL:${MQTT_TRACING_DISABLED}>:MQTT_TRACING_DISABLED>)
if(WIN32)
    target_link_libraries(yahatestclient PRIVATE ws2_32)
endif()

#  YAHA MessageStore standalone executable
add_executable(yahamsgstoreclient
    src/yaha_msgstoreclient_main.cpp
    ${MQTT_LIBRARY_SOURCES}
)

target_compile_options(yahamsgstoreclient PRIVATE ${MQTT_COMPILE_OPTIONS})
target_link_options(yahamsgstoreclient    PRIVATE ${MQTT_LINK_OPTIONS})
target_include_directories(yahamsgstoreclient PRIVATE src)
target_include_directories(yahamsgstoreclient PRIVATE ${httplib_SOURCE_DIR})
target_compile_definitions(yahamsgstoreclient PRIVATE
    $<$<BOOL:${MQTT_TRACING_DISABLED}>:MQTT_TRACING_DISABLED>)
if(WIN32)
    target_link_libraries(yahamsgstoreclient PRIVATE ws2_32)
endif()

#  External dependencies (shared by YAHA client executables, not yahabroker)
include(FetchContent)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.7
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(httplib)

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

    add_executable(yahabroker-tests
        ${MQTT_PLACEHOLDER}
        ${MQTT_TEST_SOURCES}
        ${MQTT_LIB_SOURCES}
    )

    target_compile_options(yahabroker-tests PRIVATE ${MQTT_COMPILE_OPTIONS})
    target_link_options(yahabroker-tests    PRIVATE ${MQTT_LINK_OPTIONS})
    target_compile_definitions(yahabroker-tests PRIVATE
        $<$<BOOL:${MQTT_TRACING_DISABLED}>:MQTT_TRACING_DISABLED>)
    target_link_libraries(yahabroker-tests  PRIVATE Catch2::Catch2WithMain)
    target_include_directories(yahabroker-tests PRIVATE src)
    target_include_directories(yahabroker-tests PRIVATE ${httplib_SOURCE_DIR})
    if(WIN32)
        target_link_libraries(yahabroker-tests PRIVATE ws2_32)
    endif()

    include(Catch)
    catch_discover_tests(yahabroker-tests)
endif()

#  Install 
include(GNUInstallDirs)
install(TARGETS yahabroker
    yahatestclient
    yahamsgstoreclient
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
