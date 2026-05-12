# OpenZWave phase-0 integration helper.
# This module validates vendored source + pin metadata and provides a
# reusable static target wiring for later client linking.

include(ExternalProject)
include(FetchContent)

function(yaha_prepare_openzwave_phase0)
    if(NOT EXISTS "${YAHA_OPENZWAVE_SOURCE_DIR}")
        message(FATAL_ERROR
            "Vendored OpenZWave source directory not found: ${YAHA_OPENZWAVE_SOURCE_DIR}")
    endif()

    if(NOT DEFINED YAHA_OPENZWAVE_UPSTREAM_URL OR YAHA_OPENZWAVE_UPSTREAM_URL STREQUAL "")
        message(FATAL_ERROR "YAHA_OPENZWAVE_UPSTREAM_URL must be set")
    endif()

    if(NOT DEFINED YAHA_OPENZWAVE_PIN_COMMIT OR YAHA_OPENZWAVE_PIN_COMMIT STREQUAL "")
        message(FATAL_ERROR "YAHA_OPENZWAVE_PIN_COMMIT must be set to a full commit hash")
    endif()

    set(effective_source_dir "${YAHA_OPENZWAVE_SOURCE_DIR}")
    set(effective_pin_file "${YAHA_OPENZWAVE_PIN_FILE}")

    if(NOT EXISTS "${effective_pin_file}")
        set(generated_pin_file "${CMAKE_BINARY_DIR}/generated/openzwave/PINNED_VERSION.txt")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/openzwave")
        string(TIMESTAMP pin_generated_date "%Y-%m-%d" UTC)
        file(WRITE "${generated_pin_file}"
            "upstream: ${YAHA_OPENZWAVE_UPSTREAM_URL}.git\n"
            "commit: ${YAHA_OPENZWAVE_PIN_COMMIT}\n"
            "vendored_date: ${pin_generated_date}\n"
            "note: auto-generated during CMake configure because vendored pin file was missing\n")
        set(effective_pin_file "${generated_pin_file}")
        message(WARNING
            "OpenZWave pin metadata file missing at ${YAHA_OPENZWAVE_PIN_FILE}. "
            "Generated metadata at ${generated_pin_file}.")
    endif()

    if(NOT EXISTS "${effective_source_dir}/cpp/build/Makefile")
        set(openzwave_archive_url
            "${YAHA_OPENZWAVE_UPSTREAM_URL}/archive/${YAHA_OPENZWAVE_PIN_COMMIT}.tar.gz")

        FetchContent_Declare(
            yaha_openzwave_pinned
            URL "${openzwave_archive_url}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        message(STATUS
            "Vendored OpenZWave checkout has no cpp/build/Makefile in this tree. "
            "Using pinned source snapshot: ${openzwave_archive_url}")
        FetchContent_MakeAvailable(yaha_openzwave_pinned)

        if(NOT DEFINED yaha_openzwave_pinned_SOURCE_DIR OR yaha_openzwave_pinned_SOURCE_DIR STREQUAL "")
            message(FATAL_ERROR "FetchContent did not provide yaha_openzwave_pinned_SOURCE_DIR")
        endif()

        set(effective_source_dir "${yaha_openzwave_pinned_SOURCE_DIR}")
    endif()

    if(NOT EXISTS "${effective_source_dir}/cpp/build/Makefile")
        message(FATAL_ERROR
            "OpenZWave build Makefile not found after preparation: "
            "${effective_source_dir}/cpp/build/Makefile")
    endif()

    set(YAHA_OPENZWAVE_EFFECTIVE_SOURCE_DIR
        "${effective_source_dir}"
        CACHE PATH "Effective OpenZWave source directory after preparation" FORCE)
    set(YAHA_OPENZWAVE_EFFECTIVE_PIN_FILE
        "${effective_pin_file}"
        CACHE FILEPATH "Effective OpenZWave pin metadata file after preparation" FORCE)
    set(YAHA_OPENZWAVE_EFFECTIVE_SOURCE_DIR
        "${effective_source_dir}"
        PARENT_SCOPE)
    set(YAHA_OPENZWAVE_EFFECTIVE_PIN_FILE
        "${effective_pin_file}"
        PARENT_SCOPE)
endfunction()

function(yaha_require_openzwave_phase0)
    yaha_prepare_openzwave_phase0()

    if(NOT EXISTS "${YAHA_OPENZWAVE_EFFECTIVE_SOURCE_DIR}/cpp/build/Makefile")
        message(FATAL_ERROR
            "OpenZWave build Makefile not found in effective source directory: "
            "${YAHA_OPENZWAVE_EFFECTIVE_SOURCE_DIR}/cpp/build/Makefile")
    endif()

    set(YAHA_OPENZWAVE_INCLUDE_DIR
        "${YAHA_OPENZWAVE_EFFECTIVE_SOURCE_DIR}/cpp/src"
        PARENT_SCOPE)
endfunction()

function(yaha_define_openzwave_static_target target_name)
    set(openzwave_build_dir "${CMAKE_CURRENT_BINARY_DIR}/openzwave_build")
    set(openzwave_static_path "${openzwave_build_dir}/libopenzwave.a")

    set(openzwave_make_args
        BUILD=release
        USE_HID=0
        USE_BI_TXML=1
        top_builddir=${openzwave_build_dir})

    set(openzwave_warning_suppression
        "DEBUG_CFLAGS+=-Wno-error=inconsistent-missing-override"
        "RELEASE_CFLAGS+=-Wno-error=inconsistent-missing-override")

    if(APPLE AND NOT CMAKE_CROSSCOMPILING)
        list(APPEND openzwave_make_args DARWIN_BUILD_TARGET=-arch\ arm64)
    endif()

    if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(openzwave_cross_cc "${CMAKE_C_COMPILER}")
        if(CMAKE_C_COMPILER_ARG1)
            string(APPEND openzwave_cross_cc " ${CMAKE_C_COMPILER_ARG1}")
        endif()
        if(DEFINED ZIG_TARGET_FLAGS)
            string(APPEND openzwave_cross_cc " ${ZIG_TARGET_FLAGS}")
        endif()

        set(openzwave_cross_cxx "${CMAKE_CXX_COMPILER}")
        if(CMAKE_CXX_COMPILER_ARG1)
            string(APPEND openzwave_cross_cxx " ${CMAKE_CXX_COMPILER_ARG1}")
        endif()
        if(DEFINED ZIG_TARGET_FLAGS)
            string(APPEND openzwave_cross_cxx " ${ZIG_TARGET_FLAGS}")
        endif()

        set(openzwave_build_command
            ${CMAKE_COMMAND} -E env
            "CC=${openzwave_cross_cc}"
            "CXX=${openzwave_cross_cxx}"
            "LD=${openzwave_cross_cxx}"
            "AR=${CMAKE_AR}"
            "RANLIB=${CMAKE_RANLIB}"
            make -C "${YAHA_OPENZWAVE_EFFECTIVE_SOURCE_DIR}/cpp/build"
            BITBAKE_ENV=1
            UNAME=Linux
            "RELEASE_CFLAGS+=-Wno-error=unused-command-line-argument -fno-sanitize=undefined"
            ${openzwave_warning_suppression}
            ${openzwave_make_args}
            "${openzwave_static_path}")
    else()
        set(openzwave_build_command
            make -C "${YAHA_OPENZWAVE_EFFECTIVE_SOURCE_DIR}/cpp/build"
            ${openzwave_warning_suppression}
            ${openzwave_make_args}
            "${openzwave_static_path}")
    endif()

    ExternalProject_Add(openzwave_static_build
        SOURCE_DIR "${YAHA_OPENZWAVE_EFFECTIVE_SOURCE_DIR}/cpp/build"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ${openzwave_build_command}
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS "${openzwave_static_path}"
    )

    add_library(${target_name} STATIC IMPORTED GLOBAL)
    set_target_properties(${target_name} PROPERTIES
        IMPORTED_LOCATION "${openzwave_static_path}"
        INTERFACE_INCLUDE_DIRECTORIES "${YAHA_OPENZWAVE_EFFECTIVE_SOURCE_DIR}/cpp/src"
    )
    add_dependencies(${target_name} openzwave_static_build)

    if(APPLE)
        target_link_libraries(${target_name} INTERFACE resolv)
    elseif(UNIX)
        target_link_libraries(${target_name} INTERFACE resolv pthread)
    endif()
endfunction()

function(yaha_link_openzwave_static consumer_target openzwave_target)
    target_link_libraries(${consumer_target} PRIVATE ${openzwave_target})
endfunction()
