#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <zephyr/ztest.h>

#include <solar/remote/testing/fake_dma_link.hpp>

namespace fixture
{

struct Link : solar::remote::testing::FakeDmaLink<Link, 32, 32>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0x8101},
        .name = "fixture.fake_dma",
    };
};

inline std::array<solar::remote::LinkEvent, 4> events{};
inline std::size_t event_count{};

void notify(void*, solar::remote::LinkEvent event) noexcept
{
    events[event_count++] = event;
}

} // namespace fixture

ZTEST(remote_link, test_short_dma_tx_and_rx_leases)
{
    fixture::event_count = 0;
    zassert_true(fixture::Link::open({.notify_function = &fixture::notify}).has_value());
    zassert_true(fixture::Link::connect().has_value());
    zassert_equal(fixture::events[0].kind, solar::remote::LinkEventKind::Connected);

    std::array<std::byte, 8> source{};
    for (std::size_t index{}; index < source.size(); ++index) {
        source[index] = static_cast<std::byte>(index + 1);
    }
    const solar::remote::LeaseHandle tx_handle{.slot = 2, .generation = 7};
    auto submitted = fixture::Link::try_transmit(
        solar::remote::TxLease{std::span<const std::byte>{source}, tx_handle});
    zassert_true(submitted.has_value());
    zassert_equal(*submitted, solar::remote::TxDisposition::Accepted);
    auto first = fixture::Link::advance_tx(3);
    zassert_true(first.has_value());
    zassert_equal(*first, 3);
    zassert_equal(fixture::event_count, 1);
    auto second = fixture::Link::advance_tx(3);
    zassert_true(second.has_value());
    zassert_equal(*second, 3);
    auto final = fixture::Link::advance_tx(8);
    zassert_true(final.has_value());
    zassert_equal(*final, 2);
    zassert_equal(fixture::event_count, 2);
    zassert_equal(fixture::events[1].kind, solar::remote::LinkEventKind::TxComplete);
    zassert_equal(fixture::events[1].lease, tx_handle);

    std::array<std::byte, 8> transmitted{};
    auto taken = fixture::Link::take_transmitted(transmitted);
    zassert_true(taken.has_value());
    zassert_equal(*taken, source.size());
    zassert_mem_equal(transmitted.data(), source.data(), source.size());

    const std::array rx{std::byte{0xA1}, std::byte{0xB2}, std::byte{0xC3}};
    auto injected = fixture::Link::inject(rx);
    zassert_true(injected.has_value());
    zassert_equal(fixture::events[2].kind, solar::remote::LinkEventKind::RxReady);
    auto received = fixture::Link::rx_bytes(*injected);
    zassert_true(received.has_value());
    zassert_mem_equal(received->data(), rx.data(), rx.size());
    fixture::Link::release_rx(*injected);
    zassert_false(fixture::Link::rx_bytes(*injected).has_value());
    fixture::Link::close();
}

ZTEST_SUITE(remote_link, nullptr, nullptr, nullptr, nullptr, nullptr);
