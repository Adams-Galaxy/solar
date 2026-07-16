#include <solar/remote.hpp>

struct Value
{
    int value;
};

static_assert(solar::remote::validate_schema<Value>());
