cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/../../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(solar_hardware_explicit_tests LANGUAGES CXX)

if(EXISTS ${BINARY_DIR_INCLUDE_GENERATED}/solar/hardware/generated/devicetree.hpp)
    message(FATAL_ERROR "Hardware generator output exists while generation is disabled")
endif()

target_sources(app PRIVATE src/main.cpp)
target_compile_features(app PRIVATE cxx_std_23)
set_target_properties(app PROPERTIES CXX_EXTENSIONS OFF)
