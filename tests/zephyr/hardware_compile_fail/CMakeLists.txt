cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/../../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(solar_hardware_compile_fail LANGUAGES CXX)

target_sources(app PRIVATE src/main.cpp)
target_compile_features(app PRIVATE cxx_std_23)
target_compile_definitions(app PRIVATE
    SOLAR_FAIL_CASE=0
    CONFIG_SOLAR_HARDWARE_SPI=1
    CONFIG_SOLAR_HARDWARE_I2C=1
    CONFIG_SOLAR_HARDWARE_UART=1
    CONFIG_SOLAR_HARDWARE_ADC=1
    CONFIG_SOLAR_HARDWARE_PWM=1)
set_target_properties(app PROPERTIES CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
