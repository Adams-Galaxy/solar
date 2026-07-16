cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/../../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(solar_kernel_compile_fail LANGUAGES CXX)

if(NOT DEFINED SOLAR_FAIL_CASE)
    message(FATAL_ERROR "SOLAR_FAIL_CASE must select the expected failure")
endif()

target_compile_definitions(app PRIVATE SOLAR_FAIL_CASE=${SOLAR_FAIL_CASE})
target_sources(app PRIVATE src/main.cpp)
