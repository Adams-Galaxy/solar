#include <array>
#include <cstddef>

#include <zephyr/ztest.h>

#include <solar/remote.hpp>

static_assert(!solar::remote::available);

ZTEST(solar_remote_disabled, test_cbor_reports_disabled_without_runtime_storage)
{
    std::array<std::byte, 4> output{};
    auto encoded = solar::remote::cbor::encode(solar::Status::Ok, output);
    zassert_false(encoded.has_value());
    zassert_equal(encoded.error().reason, solar::remote::Reason::Disabled);
}

ZTEST_SUITE(solar_remote_disabled, nullptr, nullptr, nullptr, nullptr, nullptr);
