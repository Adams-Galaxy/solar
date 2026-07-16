cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/../../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(solar_system LANGUAGES CXX)

target_include_directories(app PRIVATE ../../host)
target_sources(app PRIVATE src/main.cpp src/system_tu.cpp)
