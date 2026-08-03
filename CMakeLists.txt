cmake_minimum_required(VERSION 3.20)

set(SOLAR_ROOT ${CMAKE_CURRENT_LIST_DIR})
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
include(SolarCodegen)

if(DEFINED ZEPHYR_BASE AND COMMAND zephyr_include_directories)
    zephyr_include_directories(include)
    if(CONFIG_SOLAR_HARDWARE_GENERATE_DEVICETREE)
        set(solar_hardware_generated_dir
            ${BINARY_DIR_INCLUDE_GENERATED}/solar/hardware/generated)
        set(solar_hardware_generated_header
            ${solar_hardware_generated_dir}/devicetree.hpp)
        set(solar_hardware_summary
            ${CMAKE_BINARY_DIR}/solar/hardware/devicetree.json)
        add_custom_command(
            OUTPUT ${solar_hardware_generated_header}
            BYPRODUCTS ${solar_hardware_summary}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${solar_hardware_generated_dir}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/solar/hardware
            COMMAND
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_LIST_DIR}/tools/hardware/generate_devicetree.py
                --edt-pickle ${EDT_PICKLE}
                --zephyr-base ${ZEPHYR_BASE}
                --header ${solar_hardware_generated_header}
                --summary ${solar_hardware_summary}
            DEPENDS
                ${EDT_PICKLE}
                ${CMAKE_CURRENT_LIST_DIR}/tools/hardware/generate_devicetree.py
            VERBATIM)
        add_custom_target(solar_hardware_devicetree
            DEPENDS ${solar_hardware_generated_header})
        add_dependencies(app solar_hardware_devicetree)
    endif()
    if(CONFIG_SOLAR_FATAL_BRIDGE OR CONFIG_THREAD_RUNTIME_STACK_SAFETY)
        zephyr_library()
    endif()
    if(CONFIG_SOLAR_FATAL_BRIDGE)
        zephyr_library_sources(src/kernel/fatal.cpp)
    endif()
    if(CONFIG_THREAD_RUNTIME_STACK_SAFETY)
        zephyr_library_sources(src/kernel/diagnostics.cpp)
    endif()
    if(CONFIG_SOLAR_REMOTE)
        zephyr_linker_sources(SECTIONS zephyr/remote_manifest.ld)
    endif()
    if(CONFIG_SOLAR_REMOTE_GENERATE_MANIFEST)
        execute_process(
            COMMAND ${Python3_EXECUTABLE} -c "import cbor2, elftools"
            RESULT_VARIABLE solar_remote_python_modules)
        if(NOT solar_remote_python_modules EQUAL 0)
            message(FATAL_ERROR
                "Solar Remote generation requires the Python packages cbor2 and pyelftools")
        endif()
        set(solar_remote_output ${CMAKE_BINARY_DIR}/solar/remote)
        set(solar_remote_cbor ${solar_remote_output}/manifest.cbor)
        add_custom_command(
            OUTPUT ${solar_remote_cbor}
            BYPRODUCTS
                ${solar_remote_output}/manifest.json
                ${solar_remote_output}/manifest.bin
                ${solar_remote_output}/manifest.sha256
                ${solar_remote_output}/constants.py
                ${solar_remote_output}/client.py
                ${solar_remote_output}/models.py
                ${solar_remote_output}/manifest.hpp
            COMMAND
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_LIST_DIR}/tools/remote/generate_manifest.py
                --elf ${CMAKE_BINARY_DIR}/zephyr/zephyr.elf
                --output ${solar_remote_output}
            DEPENDS
                ${CMAKE_BINARY_DIR}/zephyr/zephyr.elf
                ${CMAKE_CURRENT_LIST_DIR}/tools/remote/generate_manifest.py
                ${CMAKE_CURRENT_LIST_DIR}/sdk/python/src/solar_remote/manifest.py
            VERBATIM)
        add_custom_target(solar_remote_manifest_artifacts ALL DEPENDS ${solar_remote_cbor})
    endif()

    if(CONFIG_SOLAR_APPLICATION_AUTO_INTEGRATION)
        get_filename_component(solar_application_project
            "${CONFIG_SOLAR_APPLICATION_PROJECT}" ABSOLUTE
            BASE_DIR "${APPLICATION_SOURCE_DIR}")
        get_filename_component(solar_application_lock
            "${CONFIG_SOLAR_APPLICATION_LOCK}" ABSOLUTE
            BASE_DIR "${APPLICATION_SOURCE_DIR}")

        if(EXISTS "${solar_application_project}")
            if(CONFIG_SOLAR_APPLICATION_REQUIRE_LOCK AND
               NOT EXISTS "${solar_application_lock}")
                message(FATAL_ERROR
                    "Solar application identity lock is required but missing: "
                    "${solar_application_lock}. Update it explicitly with solar-codegen "
                    "--update-lock.")
            endif()
            set(solar_application_options VALIDATE_FEATURES COMPILE_APPLICATION)
            if(NOT CONFIG_SOLAR_APPLICATION_GENERATE_PYTHON)
                list(APPEND solar_application_options NO_PYTHON)
            endif()
            if(CONFIG_SOLAR_APPLICATION_EXPLAIN)
                list(APPEND solar_application_options EXPLAIN)
            endif()
            if(CONFIG_SOLAR_REMOTE)
                list(APPEND solar_application_options REMOTE_AVAILABLE)
            endif()
            set(solar_application_output "${CMAKE_BINARY_DIR}/solar/application")
            _solar_attach_contract(
                TARGET app
                PROJECT "${solar_application_project}"
                LOCK "${solar_application_lock}"
                OUTPUT_DIR "${solar_application_output}"
                ${solar_application_options})

            if(CONFIG_SOLAR_APPLICATION_GENERATE_SHIPMENT)
                if(NOT CONFIG_SOLAR_REMOTE_GENERATE_MANIFEST)
                    message(FATAL_ERROR
                        "Solar application shipment generation requires "
                        "CONFIG_SOLAR_REMOTE_GENERATE_MANIFEST=y")
                endif()
                solar_generate_shipment(
                    TARGET solar_application_shipment
                    IR "${solar_application_output}/interface.ir.json"
                    MANIFEST_JSON "${CMAKE_BINARY_DIR}/solar/remote/manifest.json"
                    MANIFEST_BIN "${CMAKE_BINARY_DIR}/solar/remote/manifest.bin"
                    ELF "${CMAKE_BINARY_DIR}/zephyr/zephyr.elf"
                    OUTPUT_DIR "${solar_application_output}/shipment")
            endif()
        elseif(NOT CONFIG_SOLAR_APPLICATION_PROJECT STREQUAL "solar.project.yaml")
            message(FATAL_ERROR
                "Configured Solar application manifest does not exist: "
                "${solar_application_project}")
        endif()
    endif()
    return()
endif()

project(solar VERSION 0.2.0 LANGUAGES CXX)

function(solar_add_header_module target)
    add_library(${target} INTERFACE)
    target_include_directories(${target} INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
    target_compile_features(${target} INTERFACE cxx_std_23)
endfunction()

solar_add_header_module(solar_core)
solar_add_header_module(solar_kernel)
solar_add_header_module(solar_hardware)
solar_add_header_module(solar_parameters)
solar_add_header_module(solar_events)
solar_add_header_module(solar_metrics)
solar_add_header_module(solar_logging)
solar_add_header_module(solar_persistence)
solar_add_header_module(solar_execution)
solar_add_header_module(solar_supervisor)
solar_add_header_module(solar_system)
solar_add_header_module(solar_remote)
solar_add_header_module(solar_parameters_persistence)
solar_add_header_module(solar_events_execution)
solar_add_header_module(solar_metrics_export)
solar_add_header_module(solar_logging_sinks)
solar_add_header_module(solar_supervisor_probes)
solar_add_header_module(solar_remote_stream_adapters)

add_library(solar::core ALIAS solar_core)
add_library(solar::kernel ALIAS solar_kernel)
add_library(solar::hardware ALIAS solar_hardware)
add_library(solar::parameters ALIAS solar_parameters)
add_library(solar::events ALIAS solar_events)
add_library(solar::metrics ALIAS solar_metrics)
add_library(solar::logging ALIAS solar_logging)
add_library(solar::persistence ALIAS solar_persistence)
add_library(solar::execution ALIAS solar_execution)
add_library(solar::supervisor ALIAS solar_supervisor)
add_library(solar::system ALIAS solar_system)
add_library(solar::remote ALIAS solar_remote)
add_library(solar::parameters_persistence ALIAS solar_parameters_persistence)
add_library(solar::events_execution ALIAS solar_events_execution)
add_library(solar::metrics_export ALIAS solar_metrics_export)
add_library(solar::logging_sinks ALIAS solar_logging_sinks)
add_library(solar::supervisor_probes ALIAS solar_supervisor_probes)
add_library(solar::remote_stream_adapters ALIAS solar_remote_stream_adapters)

target_link_libraries(solar_kernel INTERFACE solar_core)
target_link_libraries(solar_hardware INTERFACE solar_core solar_kernel)
target_link_libraries(solar_parameters INTERFACE solar_core)
target_link_libraries(solar_events INTERFACE solar_core)
target_link_libraries(solar_metrics INTERFACE solar_core)
target_link_libraries(solar_logging INTERFACE solar_core)
target_link_libraries(solar_persistence INTERFACE solar_core)
target_link_libraries(solar_execution INTERFACE solar_core solar_kernel)
target_link_libraries(solar_supervisor INTERFACE solar_core)
target_link_libraries(solar_system INTERFACE solar_core)
target_link_libraries(solar_remote INTERFACE solar_core)
target_link_libraries(solar_parameters_persistence INTERFACE solar_parameters solar_persistence)
target_link_libraries(solar_events_execution INTERFACE solar_events solar_execution)
target_link_libraries(solar_metrics_export INTERFACE solar_metrics)
target_link_libraries(solar_logging_sinks INTERFACE solar_logging)
target_link_libraries(solar_supervisor_probes INTERFACE solar_supervisor)
target_link_libraries(solar_remote_stream_adapters INTERFACE solar_remote)

add_library(solar INTERFACE)
target_include_directories(solar INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(solar INTERFACE cxx_std_23)
target_link_libraries(solar INTERFACE solar_core solar_kernel solar_hardware
    solar_parameters solar_events solar_metrics solar_logging solar_persistence
    solar_execution solar_supervisor solar_system solar_remote)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests/host)
endif()
