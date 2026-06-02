cmake_minimum_required(VERSION 3.20)

if(DEFINED ZEPHYR_BASE AND COMMAND zephyr_include_directories)
    zephyr_include_directories(include)
    return()
endif()

project(solar VERSION 0.1.0 LANGUAGES CXX)

add_library(solar INTERFACE)
target_include_directories(solar INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(solar INTERFACE cxx_std_20)
