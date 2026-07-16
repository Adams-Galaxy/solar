cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/../../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(solar_hardware_foundation_tests LANGUAGES CXX)

target_sources(app PRIVATE src/main.cpp)
target_compile_features(app PRIVATE cxx_std_23)
set_target_properties(app PROPERTIES CXX_EXTENSIONS OFF)

if(CONFIG_SOLAR_HARDWARE_GENERATE_DEVICETREE)
    add_custom_command(TARGET app POST_BUILD
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_LIST_DIR}/check_generator.py
            --generator ${CMAKE_CURRENT_LIST_DIR}/../../../tools/hardware/generate_devicetree.py
            --edt-pickle ${EDT_PICKLE}
            --zephyr-base ${ZEPHYR_BASE}
            --header ${BINARY_DIR_INCLUDE_GENERATED}/solar/hardware/generated/devicetree.hpp
            --summary ${CMAKE_BINARY_DIR}/solar/hardware/devicetree.json
        VERBATIM)
endif()
