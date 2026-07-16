cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/../../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(solar_inspection_compile_fail LANGUAGES CXX)

if(NOT DEFINED SOLAR_FAIL_CASE)
    set(SOLAR_FAIL_CASE 0)
endif()

target_compile_definitions(app PRIVATE SOLAR_FAIL_CASE=${SOLAR_FAIL_CASE})
target_sources(app PRIVATE src/main.cpp)
target_compile_features(app PRIVATE cxx_std_23)
set_target_properties(app PROPERTIES CXX_EXTENSIONS OFF)

