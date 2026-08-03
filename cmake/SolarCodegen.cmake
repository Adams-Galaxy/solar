include_guard(GLOBAL)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

get_filename_component(_SOLAR_CODEGEN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SOLAR_CODEGEN_ROOT "${_SOLAR_CODEGEN_ROOT}" CACHE INTERNAL
    "Solar source root used by contract generation")

function(_solar_attach_contract)
    set(options UPDATE_LOCK NO_PYTHON EXPLAIN VALIDATE_FEATURES REMOTE_AVAILABLE
        COMPILE_APPLICATION)
    set(one_value_args TARGET PROJECT LOCK OUTPUT_DIR)
    cmake_parse_arguments(SOLAR_GEN "${options}" "${one_value_args}" "" ${ARGN})

    foreach(required TARGET PROJECT LOCK)
        if(NOT SOLAR_GEN_${required})
            message(FATAL_ERROR "Solar contract integration requires ${required}")
        endif()
    endforeach()
    if(NOT TARGET ${SOLAR_GEN_TARGET})
        message(FATAL_ERROR
            "Solar contract integration target does not exist: ${SOLAR_GEN_TARGET}")
    endif()

    if(SOLAR_GEN_OUTPUT_DIR)
        set(output_dir "${SOLAR_GEN_OUTPUT_DIR}")
    else()
        set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/solar-generated")
    endif()
    set(application_header "${output_dir}/solar/generated/application.hpp")
    set(app_header "${output_dir}/solar/generated/app.hpp")
    set(remote_header "${output_dir}/solar/generated/remote.hpp")
    set(application_source "${output_dir}/solar/generated/application.cpp")
    set(depfile "${output_dir}/interface.d")
    set(update_lock_argument)
    if(SOLAR_GEN_UPDATE_LOCK)
        set(update_lock_argument --update-lock)
    endif()
    set(python_argument)
    if(SOLAR_GEN_NO_PYTHON)
        set(python_argument --no-python)
    endif()
    set(explain_argument)
    set(explain_byproducts)
    if(SOLAR_GEN_EXPLAIN)
        set(explain_argument --explain)
        set(explain_byproducts
            "${output_dir}/application.json"
            "${output_dir}/application.txt")
    endif()
    set(feature_arguments)
    if(SOLAR_GEN_VALIDATE_FEATURES)
        list(APPEND feature_arguments --validate-features)
        if(SOLAR_GEN_REMOTE_AVAILABLE)
            list(APPEND feature_arguments --available-feature remote)
        endif()
    endif()
    set(lock_dependency)
    if(EXISTS "${SOLAR_GEN_LOCK}")
        set(lock_dependency "${SOLAR_GEN_LOCK}")
    endif()

    add_custom_command(
        OUTPUT "${application_header}"
        BYPRODUCTS
            "${app_header}"
            "${remote_header}"
            "${application_source}"
            "${output_dir}/solar/generated/types.hpp"
            "${output_dir}/solar/generated/parameters.hpp"
            "${output_dir}/solar/generated/contract.hpp"
            "${output_dir}/interface.ir.json"
            "${output_dir}/manifest.json"
            "${output_dir}/manifest.bin"
            "${output_dir}/manifest.sha256"
            "${output_dir}/compatibility.json"
            ${explain_byproducts}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${output_dir}"
        COMMAND ${Python3_EXECUTABLE}
            "${SOLAR_CODEGEN_ROOT}/tools/generate_solar.py"
            --project "${SOLAR_GEN_PROJECT}"
            --output "${output_dir}"
            --lock "${SOLAR_GEN_LOCK}"
            ${update_lock_argument}
            ${python_argument}
            ${explain_argument}
            ${feature_arguments}
        DEPENDS
            "${SOLAR_GEN_PROJECT}"
            ${lock_dependency}
            "${SOLAR_CODEGEN_ROOT}/tools/generate_solar.py"
            "${SOLAR_CODEGEN_ROOT}/tools/solar_codegen/compiler.py"
        DEPFILE "${depfile}"
        VERBATIM)

    string(MAKE_C_IDENTIFIER "${SOLAR_GEN_TARGET}_solar_contract" generation_target)
    add_custom_target(${generation_target} DEPENDS "${application_header}")
    add_dependencies(${SOLAR_GEN_TARGET} ${generation_target})
    if(SOLAR_GEN_COMPILE_APPLICATION)
        set_source_files_properties("${application_source}" PROPERTIES GENERATED TRUE)
        target_sources(${SOLAR_GEN_TARGET} PRIVATE "${application_source}")
    endif()
    target_include_directories(${SOLAR_GEN_TARGET} PRIVATE "${output_dir}")
    target_compile_definitions(${SOLAR_GEN_TARGET} PRIVATE
        SOLAR_HAS_GENERATED_APPLICATION=1)
    set_property(TARGET ${SOLAR_GEN_TARGET} APPEND PROPERTY
        ADDITIONAL_CLEAN_FILES "${output_dir}")
    set_property(TARGET ${SOLAR_GEN_TARGET} PROPERTY
        SOLAR_APPLICATION_OUTPUT_DIR "${output_dir}")
    set_property(TARGET ${SOLAR_GEN_TARGET} PROPERTY
        SOLAR_APPLICATION_GENERATION_TARGET "${generation_target}")
endfunction()

# Generate a Solar application contract into one build directory.
#
# solar_generate_contract(
#   TARGET <cmake-target>
#   PROJECT <solar.project.yaml>
#   LOCK <solar.interface.lock>
#   [OUTPUT_DIR <directory>]
#   [UPDATE_LOCK] [NO_PYTHON] [EXPLAIN])
function(solar_generate_contract)
    _solar_attach_contract(${ARGN})
endfunction()

# Verify a linked Zephyr image against its authored IR and generate the exact
# application client package shipped with that firmware.
function(solar_generate_shipment)
    set(options)
    set(one_value_args TARGET IR MANIFEST_JSON MANIFEST_BIN ELF OUTPUT_DIR)
    cmake_parse_arguments(SOLAR_SHIP "${options}" "${one_value_args}" "" ${ARGN})
    foreach(required TARGET IR MANIFEST_JSON MANIFEST_BIN ELF OUTPUT_DIR)
        if(NOT SOLAR_SHIP_${required})
            message(FATAL_ERROR "solar_generate_shipment requires ${required}")
        endif()
    endforeach()

    set(shipment "${SOLAR_SHIP_OUTPUT_DIR}/shipment.json")
    add_custom_command(
        OUTPUT "${shipment}"
        BYPRODUCTS
            "${SOLAR_SHIP_OUTPUT_DIR}/manifest.json"
            "${SOLAR_SHIP_OUTPUT_DIR}/manifest.bin"
            "${SOLAR_SHIP_OUTPUT_DIR}/manifest.sha256"
            "${SOLAR_SHIP_OUTPUT_DIR}/compatibility.json"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SOLAR_SHIP_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E env
            "PYTHONPATH=${SOLAR_CODEGEN_ROOT}/tools"
            ${Python3_EXECUTABLE} -m solar_codegen.shipment
            --ir "${SOLAR_SHIP_IR}"
            --manifest-json "${SOLAR_SHIP_MANIFEST_JSON}"
            --manifest-bin "${SOLAR_SHIP_MANIFEST_BIN}"
            --elf "${SOLAR_SHIP_ELF}"
            --output "${SOLAR_SHIP_OUTPUT_DIR}"
        DEPENDS
            "${SOLAR_SHIP_IR}"
            "${SOLAR_SHIP_MANIFEST_JSON}"
            "${SOLAR_SHIP_MANIFEST_BIN}"
            "${SOLAR_SHIP_ELF}"
            "${SOLAR_CODEGEN_ROOT}/tools/solar_codegen/shipment.py"
            "${SOLAR_CODEGEN_ROOT}/tools/solar_codegen/compiler.py"
        VERBATIM)
    add_custom_target(${SOLAR_SHIP_TARGET} ALL DEPENDS "${shipment}")
endfunction()
