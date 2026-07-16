#include <expected>

#include <solar/solar.hpp>

static_assert(__cplusplus >= 202100L);
static_assert(solar::version == solar::Version{0, 1, 0});

int main()
{
    std::expected<int, int> value{42};
    return value.value() == 42 ? 0 : 1;
}
