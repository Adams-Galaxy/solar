#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <solar/remote.hpp>
#include <solar/remote/testing/in_memory_link.hpp>
#include <solar/solar.hpp>

namespace fixture
{

struct Sample
{
    std::uint32_t sequence{};
    std::int16_t value{};
};

struct LoanedData
{
    static constexpr solar::remote::DataDescriptor descriptor{
        .id = solar::remote::DataId{0xA101},
        .name = "fixture.loaned",
    };
    using Value = Sample;
    using Capabilities = solar::remote::Capabilities<solar::remote::OutStream<
        solar::remote::Loaned<solar::remote::LoanedPool<2, 16>>,
        solar::remote::MaxRate<1000>>>;
};

struct TestLink : solar::remote::testing::InMemoryLink<TestLink, 256, 256>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0xA001},
        .name = "fixture.loan.memory",
    };
    using Grants = solar::remote::Requires<solar::remote::permission::Observe>;
};

struct Root
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.loan.root"};
    using RemoteData = solar::remote::ContributeData<LoanedData>;
    using RemoteLinks = solar::remote::ContributeLinks<TestLink>;
};

using System = solar::System<solar::Blueprint<solar::Facilities<Root>>>;
using Service = typename System::RemoteService;

} // namespace fixture

template <> struct solar::remote::Schema<fixture::Sample>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0xA201},
        .name = "fixture.LoanSample",
    };
    using Fields = remote::Fields<Field<1, &fixture::Sample::sequence>,
                                  Field<2, &fixture::Sample::value>>;
    static constexpr std::size_t max_encoded_size = 6;
    static constexpr Codec codec = Codec::Packed;
};

SOLAR_BIND_SYSTEM(fixture::System);

namespace
{

std::array<std::byte, 256> host_bytes{};
std::array<std::byte, 160> decoded_bytes{};

solar::Result<solar::remote::frame::Decoded, solar::remote::Error>
receive_frame(int attempts = 300)
{
    for (int attempt = 0; attempt < attempts; ++attempt) {
        auto bytes = fixture::TestLink::take_transmitted(host_bytes);
        if (bytes) {
            return solar::remote::frame::decode(std::span{host_bytes}.first(*bytes),
                                                decoded_bytes);
        }
        k_sleep(K_MSEC(1));
    }
    return solar::fail(solar::remote::Error{.status = solar::Status::Timeout});
}

void inject(const solar::remote::protocol::Envelope& envelope,
            std::span<const std::byte> payload = {})
{
    std::array<std::byte, 160> scratch{};
    std::array<std::byte, 256> encoded{};
    auto size = solar::remote::frame::encode(envelope, payload, scratch, encoded);
    zassert_true(size.has_value());
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (fixture::TestLink::inject(std::span{encoded}.first(*size))) {
            return;
        }
        k_sleep(K_MSEC(1));
    }
    zassert_unreachable("host frame was not admitted");
}

void establish_subscription()
{
    zassert_equal(fixture::TestLink::connect(), solar::Status::Ok);
    zassert_true(receive_frame().has_value());

    solar::remote::protocol::Envelope hello{
        .kind = solar::remote::protocol::Kind::ClientHello,
        .frame_sequence = 1,
        .fragment_count = 1,
    };
    constexpr auto hello_payload = fixture::Service::hello_payload();
    inject(hello, hello_payload);
    zassert_true(receive_frame().has_value());

    solar::remote::protocol::Envelope subscribe{
        .kind = solar::remote::protocol::Kind::Subscribe,
        .session_epoch = 1,
        .frame_sequence = 2,
        .target = fixture::LoanedData::descriptor.id.value,
        .request_id = 1,
        .fragment_count = 1,
    };
    inject(subscribe);
    auto response = receive_frame();
    zassert_true(response.has_value());
    zassert_equal(response->envelope.kind, solar::remote::protocol::Kind::Response);

    solar::remote::protocol::Envelope ack{
        .kind = solar::remote::protocol::Kind::ResponseAck,
        .session_epoch = 1,
        .frame_sequence = 3,
        .target = fixture::LoanedData::descriptor.id.value,
        .request_id = 1,
        .fragment_count = 1,
    };
    inject(ack);
}

} // namespace

ZTEST(remote_loan, test_bounded_generation_checked_loan_lifecycle)
{
    zassert_true(fixture::System::boot().has_value());

    auto unsubscribed = solar::remote::try_loan<fixture::LoanedData>();
    zassert_true(unsubscribed.has_value());
    auto unsubscribed_loan = std::move(*unsubscribed);
    auto no_subscribers = solar::remote::commit<fixture::LoanedData>(
        std::move(unsubscribed_loan), 0);
    zassert_true(no_subscribers.has_value());
    zassert_equal(no_subscribers->disposition,
                  solar::remote::WriteDisposition::NoSubscribers);

    auto first_result = solar::remote::try_loan<fixture::LoanedData>();
    auto second_result = solar::remote::try_loan<fixture::LoanedData>();
    auto exhausted = solar::remote::try_loan<fixture::LoanedData>();
    zassert_true(first_result.has_value());
    zassert_true(second_result.has_value());
    zassert_false(exhausted.has_value());
    zassert_equal(exhausted.error().status, solar::Status::NoBuffer);

    auto first = std::move(*first_result);
    auto second = std::move(*second_result);
    const auto old_slot = first.slot();
    const auto old_generation = first.generation();
    const auto old_bytes = first.data();
    first = {};

    auto replacement_result = solar::remote::try_loan<fixture::LoanedData>();
    zassert_true(replacement_result.has_value());
    auto replacement = std::move(*replacement_result);
    zassert_equal(replacement.slot(), old_slot);
    zassert_not_equal(replacement.generation(), old_generation);

    auto stale = solar::remote::Loan<fixture::LoanedData>::make(
        old_bytes, old_slot, old_generation,
        &solar::remote::detail::abandon_loan<fixture::System, fixture::LoanedData>);
    auto stale_commit = solar::remote::commit<fixture::LoanedData>(std::move(stale), 0);
    zassert_false(stale_commit.has_value());
    zassert_equal(stale_commit.error().status, solar::Status::Invalid);

    establish_subscription();

    constexpr fixture::Sample expected{.sequence = 42, .value = -17};
    auto encoded = solar::remote::packed::encode(expected, replacement.data());
    zassert_true(encoded.has_value());
    auto committed = solar::remote::commit<fixture::LoanedData>(
        std::move(replacement), *encoded);
    zassert_true(committed.has_value());
    zassert_true(committed->wake_queued);

    auto data = receive_frame();
    zassert_true(data.has_value());
    zassert_equal(data->envelope.kind, solar::remote::protocol::Kind::Data);
    zassert_true((static_cast<std::uint8_t>(data->envelope.flags) &
                  static_cast<std::uint8_t>(solar::remote::protocol::Flags::PackedPayload)) != 0);
    auto decoded = solar::remote::packed::decode<fixture::Sample>(data->payload);
    zassert_true(decoded.has_value());
    zassert_equal(decoded->sequence, expected.sequence);
    zassert_equal(decoded->value, expected.value);

    auto released = solar::remote::try_loan<fixture::LoanedData>();
    zassert_true(released.has_value());
    {
        auto& state = solar::remote::detail::loan_state<fixture::System, fixture::LoanedData>();
        auto guard = state.lock.acquire();
        zassert_equal(state.abandoned, 1);
        zassert_equal(state.committed, 2);
        zassert_equal(state.released, 2);
    }

    second = {};
    released = solar::fail(solar::remote::Error{});
    zassert_true(fixture::System::stop().has_value());
}

ZTEST_SUITE(remote_loan, nullptr, nullptr, nullptr, nullptr, nullptr);
