cmake_minimum_required(VERSION 3.20)

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
    if(CONFIG_SOLAR_FATAL_BRIDGE OR CONFIG_SOLAR_LOG)
        zephyr_library()
    endif()
    if(CONFIG_SOLAR_FATAL_BRIDGE)
        zephyr_library_sources(src/kernel/fatal.cpp)
    endif()
    if(CONFIG_SOLAR_LOG)
        zephyr_library_sources(src/log/frontend.cpp)
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
                ${solar_remote_output}/manifest.sha256
                ${solar_remote_output}/constants.py
                ${solar_remote_output}/client.py
                ${solar_remote_output}/manifest.hpp
            COMMAND
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_LIST_DIR}/tools/remote/generate_manifest.py
                --elf ${CMAKE_BINARY_DIR}/zephyr/zephyr.elf
                --output ${solar_remote_output}
            DEPENDS
                ${CMAKE_BINARY_DIR}/zephyr/zephyr.elf
                ${CMAKE_CURRENT_LIST_DIR}/tools/remote/generate_manifest.py
            VERBATIM)
        add_custom_target(solar_remote_manifest_artifacts ALL DEPENDS ${solar_remote_cbor})
    endif()
    return()
endif()

project(solar VERSION 0.1.0 LANGUAGES CXX)

add_library(solar INTERFACE)
target_include_directories(solar INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(solar INTERFACE cxx_std_23)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests/host)
endif()
