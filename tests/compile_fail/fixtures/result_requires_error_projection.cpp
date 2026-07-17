#include <solar/core/status.hpp>

struct MissingProjection
{};

static_assert(solar::ErrorType<MissingProjection>,
              "SOLAR_DIAGNOSTIC_RESULT_REQUIRES_ERROR_PROJECTION: Result error types must expose "
              "a noexcept status_of(error) projection");
