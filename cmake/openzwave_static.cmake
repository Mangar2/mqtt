# OpenZWave phase-0 integration helper.
# This module validates the vendored source and pin metadata and provides
# standard variables for later static target integration.

function(yaha_require_openzwave_phase0)
    if(NOT EXISTS "${YAHA_OPENZWAVE_SOURCE_DIR}")
        message(FATAL_ERROR
            "Vendored OpenZWave source directory not found: ${YAHA_OPENZWAVE_SOURCE_DIR}")
    endif()

    if(NOT EXISTS "${YAHA_OPENZWAVE_PIN_FILE}")
        message(FATAL_ERROR
            "OpenZWave pin metadata file not found: ${YAHA_OPENZWAVE_PIN_FILE}")
    endif()

    set(YAHA_OPENZWAVE_INCLUDE_DIR
        "${YAHA_OPENZWAVE_SOURCE_DIR}/cpp/src"
        PARENT_SCOPE)

    set(YAHA_OPENZWAVE_NOTE
        "Phase 0 validated: vendored source + pin metadata are present."
        PARENT_SCOPE)
endfunction()
