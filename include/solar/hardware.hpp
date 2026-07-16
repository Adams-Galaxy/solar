#pragma once

#include "solar/hardware/async.hpp"
#include "solar/hardware/dt.hpp"
#include "solar/hardware/endpoint.hpp"
#include "solar/hardware/error.hpp"
#include "solar/hardware/types.hpp"

#if defined(CONFIG_SOLAR_HARDWARE_GPIO)
#include "solar/hardware/gpio.hpp"
#endif
#if defined(CONFIG_SOLAR_HARDWARE_SPI)
#include "solar/hardware/spi.hpp"
#endif
#if defined(CONFIG_SOLAR_HARDWARE_I2C)
#include "solar/hardware/i2c.hpp"
#endif
#if defined(CONFIG_SOLAR_HARDWARE_ADC)
#include "solar/hardware/adc.hpp"
#endif
#if defined(CONFIG_SOLAR_HARDWARE_PWM)
#include "solar/hardware/pwm.hpp"
#endif
#if defined(CONFIG_SOLAR_HARDWARE_UART)
#include "solar/hardware/uart.hpp"
#endif
#if defined(CONFIG_SOLAR_HARDWARE_COUNTER)
#include "solar/hardware/counter.hpp"
#endif
#if defined(CONFIG_SOLAR_HARDWARE_WATCHDOG)
#include "solar/hardware/watchdog.hpp"
#endif
#if defined(CONFIG_SOLAR_HARDWARE_RTIO)
#include "solar/hardware/rtio.hpp"
#endif
