#pragma once

#include <version>

#if __cplusplus < 202100L
#error "SOLAR_DIAGNOSTIC_REQUIRES_CPP23: Solar requires C++23 or newer"
#endif

#ifndef __cpp_lib_expected
#error "SOLAR_DIAGNOSTIC_REQUIRES_EXPECTED: Solar requires the C++23 <expected> library"
#endif

#if __cpp_lib_expected < 202211L
#error "SOLAR_DIAGNOSTIC_REQUIRES_MONADIC_EXPECTED: Solar requires monadic std::expected operations"
#endif
