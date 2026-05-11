# OpenZWave phase-0 integration helper.
# This module validates vendored source + pin metadata and provides a
# reusable static target wiring for later client linking.

include(ExternalProject)

function(yaha_require_openzwave_phase0)
    if(NOT EXISTS "${YAHA_OPENZWAVE_SOURCE_DIR}")
        message(FATAL_ERROR
            "Vendored OpenZWave source directory not found: ${YAHA_OPENZWAVE_SOURCE_DIR}")
    endif()

    if(NOT EXISTS "${YAHA_OPENZWAVE_PIN_FILE}")
        message(FATAL_ERROR
            "OpenZWave pin metadata file not found: ${YAHA_OPENZWAVE_PIN_FILE}")
    endif()

    if(NOT EXISTS "${YAHA_OPENZWAVE_SOURCE_DIR}/cpp/build/Makefile")
        message(FATAL_ERROR
            "OpenZWave build Makefile not found: ${YAHA_OPENZWAVE_SOURCE_DIR}/cpp/build/Makefile")
    endif()

    set(YAHA_OPENZWAVE_INCLUDE_DIR
        "${YAHA_OPENZWAVE_SOURCE_DIR}/cpp/src"
        PARENT_SCOPE)
endfunction()

function(yaha_define_openzwave_static_target target_name)
    set(openzwave_static_path "${YAHA_OPENZWAVE_SOURCE_DIR}/cpp/build/libopenzwave.a")

    set(openzwave_build_command
        make -C "${YAHA_OPENZWAVE_SOURCE_DIR}/cpp/build" BUILD=release USE_HID=0 USE_BI_TXML=1)
    if(APPLE)
        list(APPEND openzwave_build_command DARWIN_BUILD_TARGET=-arch\ arm64)
    endif()

    ExternalProject_Add(openzwave_static_build
        SOURCE_DIR "${YAHA_OPENZWAVE_SOURCE_DIR}/cpp/build"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ${openzwave_build_command}
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS "${openzwave_static_path}"
    )

    add_library(${target_name} STATIC IMPORTED GLOBAL)
    set_target_properties(${target_name} PROPERTIES
        IMPORTED_LOCATION "${openzwave_static_path}"
        INTERFACE_INCLUDE_DIRECTORIES "${YAHA_OPENZWAVE_SOURCE_DIR}/cpp/src"
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
