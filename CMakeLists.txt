cmake_minimum_required(VERSION 3.20)

project(solar VERSION 0.1.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

option(SOLAR_BUILD_TESTS "Build Solar host tests" ON)
set(SOLAR_LOW_LEVEL "SIMULATED" CACHE STRING "Low-level backend define: SIMULATED, TEENSYDUINO, or NONE")
set_property(CACHE SOLAR_LOW_LEVEL PROPERTY STRINGS SIMULATED TEENSYDUINO NONE)
set(SOLAR_LOW_LEVEL_INCLUDE_DIR "" CACHE PATH "Optional include root that provides low_level/... headers")

add_library(solar INTERFACE)
target_include_directories(solar INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(solar INTERFACE cxx_std_20)
target_link_libraries(solar INTERFACE Threads::Threads)

if(SOLAR_LOW_LEVEL_INCLUDE_DIR)
    target_include_directories(solar INTERFACE ${SOLAR_LOW_LEVEL_INCLUDE_DIR})
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/include/low_level)
    target_include_directories(solar INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/include)
endif()

if(SOLAR_LOW_LEVEL STREQUAL "SIMULATED")
    target_compile_definitions(solar INTERFACE LOW_LEVEL_SIMULATED)
elseif(SOLAR_LOW_LEVEL STREQUAL "TEENSYDUINO")
    target_compile_definitions(solar INTERFACE LOW_LEVEL_TEENSYDUINO)
elseif(NOT SOLAR_LOW_LEVEL STREQUAL "NONE")
    message(FATAL_ERROR "SOLAR_LOW_LEVEL must be SIMULATED, TEENSYDUINO, or NONE")
endif()

if(SOLAR_BUILD_TESTS)
    enable_testing()

    add_executable(solar_static_graph_tests tests/host/static_graph_tests.cpp)
    target_link_libraries(solar_static_graph_tests PRIVATE solar)
    add_test(NAME solar_static_graph_tests COMMAND solar_static_graph_tests)
endif()
