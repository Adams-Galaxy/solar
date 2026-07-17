#include <cerrno>
#include <zephyr/ztest.h>

#include <solar/core.hpp>

namespace
{

enum class SensorError
{
    Offline,
};

constexpr solar::Status status_of(SensorError) noexcept
{
    return solar::Status::NotReady;
}

struct MoveOnlyValue
{
    int value;

    constexpr explicit MoveOnlyValue(int item) : value(item) {}
    MoveOnlyValue(const MoveOnlyValue&) = delete;
    constexpr MoveOnlyValue(MoveOnlyValue&&) = default;
    MoveOnlyValue& operator=(const MoveOnlyValue&) = delete;
    constexpr MoveOnlyValue& operator=(MoveOnlyValue&&) = default;
};

constexpr solar::Result<int, SensorError> reading(bool ready)
{
    return ready ? solar::Result<int, SensorError>{21}
                 : solar::fail<SensorError>(SensorError::Offline);
}

static_assert(reading(true).transform([](int value) { return value * 2; }).value() == 42);
static_assert(solar::status_from_errno(-ETIMEDOUT) == solar::Status::Timeout);
static_assert(solar::to_errno(solar::Status::Busy) == EBUSY);

} // namespace

ZTEST(solar_core, test_expected_composition)
{
    const auto result =
        reading(true)
            .and_then([](int value) -> solar::Result<int, SensorError> { return value + 1; })
            .transform([](int value) { return value * 2; });

    zassert_true(result.has_value());
    zassert_equal(*result, 44);
}

ZTEST(solar_core, test_void_and_move_only_results)
{
    solar::Result<void> complete{};
    solar::Result<MoveOnlyValue> value{std::in_place, 42};
    auto moved = std::move(value).transform([](MoveOnlyValue item) { return item.value; });

    zassert_true(complete.has_value());
    zassert_true(moved.has_value());
    zassert_equal(*moved, 42);
}

ZTEST(solar_core, test_status_conversion)
{
    zassert_equal(solar::status_from_errno(-EINVAL), solar::Status::Invalid);
    zassert_equal(solar::status_from_errno(EIO), solar::Status::Error);
    zassert_equal(solar::to_native_errno(solar::Status::NotReady), -ENODEV);
}

ZTEST_SUITE(solar_core, nullptr, nullptr, nullptr, nullptr, nullptr);
