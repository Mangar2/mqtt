cmake_minimum_required(VERSION 3.25)

project(mqtt-broker
    VERSION 0.1.0
    DESCRIPTION "MQTT 5.0 Broker"
    LANGUAGES CXX
)

# ── Language standard ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Options ────────────────────────────────────────────────────────────────────
option(MQTT_SANITIZE "Enable AddressSanitizer and UBSan (host builds only)" OFF)
option(MQTT_COVERAGE "Enable clang source-based coverage instrumentation"   OFF)

# ── Shared compiler/linker flags ───────────────────────────────────────────────
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

# ── Main executable ────────────────────────────────────────────────────────────
add_executable(mqtt-broker
    src/main.cpp
)

target_compile_options(mqtt-broker PRIVATE ${MQTT_COMPILE_OPTIONS})
target_link_options(mqtt-broker    PRIVATE ${MQTT_LINK_OPTIONS})

# ── Unit tests ─────────────────────────────────────────────────────────────────
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

    add_executable(mqtt-broker-tests
        tests/unit/test_placeholder.cpp
    )

    target_compile_options(mqtt-broker-tests PRIVATE ${MQTT_COMPILE_OPTIONS})
    target_link_options(mqtt-broker-tests    PRIVATE ${MQTT_LINK_OPTIONS})
    target_link_libraries(mqtt-broker-tests  PRIVATE Catch2::Catch2WithMain)

    include(Catch)
    catch_discover_tests(mqtt-broker-tests)
endif()

# ── Install ────────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS mqtt-broker
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
