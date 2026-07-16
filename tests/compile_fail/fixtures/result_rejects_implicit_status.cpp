#include <type_traits>

#include <solar/core/status.hpp>

static_assert(std::is_constructible_v<solar::Result<int>, solar::Status>,
              "SOLAR_DIAGNOSTIC_RESULT_REJECTS_IMPLICIT_STATUS: failures require solar::fail or "
              "std::unexpected");
