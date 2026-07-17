#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <solar/remote.hpp>
#include <solar/remote/testing/in_memory_link.hpp>
#include <solar/solar.hpp>

namespace fixture
{

struct Change
{
    std::uint32_t sequence{};
};

struct Watched
{
    static constexpr solar::remote::DataDescriptor descriptor{
        .id = solar::remote::DataId{0xB101},
        .name = "fixture.watched",
    };
    using Value = Change;
    using Capabilities = solar::remote::Capabilities<
        solar::remote::Watch<solar::remote::Queue<2, solar::remote::Reject>>>;
};

struct Notice
{
    static constexpr solar::remote::TopicDescriptor descriptor{
        .id = solar::remote::TopicId{0xB101},
        .name = "fixture.notice",
    };
    using Value = Change;
    using Publication = solar::remote::Watch<solar::remote::Queue<2, solar::remote::Reject>>;
};

struct TestLink : solar::remote::testing::InMemoryLink<TestLink, 256, 256>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0xB001},
        .name = "fixture.topic.memory",
    };
    using Grants = solar::remote::Requires<solar::remote::permission::Observe>;
};

struct Root
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.topic.root"};
    using RemoteData = solar::remote::ContributeData<Watched>;
    using RemoteTopics = solar::remote::ContributeTopics<Notice>;
    using RemoteLinks = solar::remote::ContributeLinks<TestLink>;
};

using System = solar::System<solar::Blueprint<solar::Facilities<Root>>>;
using Service = typename System::RemoteService;

} // namespace fixture

template <> struct solar::remote::Schema<fixture::Change>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0xB201},
        .name = "fixture.Change",
    };
    using Fields = remote::Fields<Field<1, &fixture::Change::sequence>>;
    static constexpr std::size_t max_encoded_size = 12;
    static constexpr Codec codec = Codec::Cbor;
};

SOLAR_BIND_SYSTEM(fixture::System);

namespace
{

std::array<std::byte, 256> host_bytes{};
std::array<std::byte, 160> decoded_bytes{};

solar::Result<solar::remote::frame::Decoded, solar::remote::Error> receive_frame(int attempts = 300)
{
    for (int attempt = 0; attempt < attempts; ++attempt) {
        auto bytes = fixture::TestLink::take_transmitted(host_bytes);
        if (bytes) {
            return solar::remote::frame::decode(std::span{host_bytes}.first(*bytes), decoded_bytes);
        }
        k_sleep(K_MSEC(1));
    }
    return solar::fail<solar::remote::Error>({.status = solar::Status::Timeout});
}

void inject(const solar::remote::protocol::Envelope& envelope)
{
    std::array<std::byte, 160> scratch{};
    std::array<std::byte, 256> encoded{};
    auto size = solar::remote::frame::encode(envelope, {}, scratch, encoded);
    zassert_true(size.has_value());
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (fixture::TestLink::inject(std::span{encoded}.first(*size))) {
            return;
        }
        k_sleep(K_MSEC(1));
    }
    zassert_unreachable("host frame was not admitted");
}

void subscribe(solar::remote::protocol::SubscriptionKind kind, std::uint32_t request_id)
{
    solar::remote::protocol::Envelope request{
        .kind = solar::remote::protocol::Kind::Subscribe,
        .session_epoch = 1,
        .frame_sequence = request_id * 2,
        .target = 0xB101,
        .request_id = request_id,
        .fragment_count = 1,
    };
    request.set_subscription(kind);
    inject(request);
    auto response = receive_frame();
    zassert_true(response.has_value());
    zassert_equal(response->envelope.kind, solar::remote::protocol::Kind::Response);

    request.kind = solar::remote::protocol::Kind::ResponseAck;
    request.frame_sequence += 1;
    inject(request);
}

void unsubscribe(solar::remote::protocol::SubscriptionKind kind, std::uint32_t request_id)
{
    solar::remote::protocol::Envelope request{
        .kind = solar::remote::protocol::Kind::Unsubscribe,
        .session_epoch = 1,
        .frame_sequence = request_id * 2,
        .target = 0xB101,
        .request_id = request_id,
        .fragment_count = 1,
    };
    request.set_subscription(kind);
    inject(request);
    auto response = receive_frame();
    zassert_true(response.has_value());
    zassert_equal(response->envelope.kind, solar::remote::protocol::Kind::Response);
}

} // namespace

ZTEST(remote_topics, test_watch_and_topic_have_independent_subscription_domains)
{
    zassert_true(fixture::System::boot().has_value());
    zassert_false(solar::remote::interested<fixture::Watched>());
    zassert_false(solar::remote::interested<fixture::Notice>());

    zassert_true(fixture::TestLink::connect().has_value());
    zassert_true(receive_frame().has_value());
    solar::remote::protocol::Envelope hello{
        .kind = solar::remote::protocol::Kind::ClientHello,
        .frame_sequence = 1,
        .fragment_count = 1,
    };
    constexpr auto hello_payload = fixture::Service::hello_payload();
    {
        std::array<std::byte, 160> scratch{};
        std::array<std::byte, 256> encoded{};
        auto size = solar::remote::frame::encode(hello, hello_payload, scratch, encoded);
        zassert_true(size.has_value());
        auto admitted = fixture::TestLink::inject(std::span{encoded}.first(*size));
        zassert_true(admitted.has_value());
    }
    zassert_true(receive_frame().has_value());

    subscribe(solar::remote::protocol::SubscriptionKind::DataWatch, 1);
    zassert_true(solar::remote::interested<fixture::Watched>());
    zassert_false(solar::remote::interested<fixture::Notice>());
    subscribe(solar::remote::protocol::SubscriptionKind::Topic, 2);
    zassert_true(solar::remote::interested<fixture::Notice>());

    zassert_true(solar::remote::publish<fixture::Watched>({.sequence = 11}).has_value());
    zassert_true(solar::remote::publish<fixture::Notice>({.sequence = 22}).has_value());

    bool saw_watch{};
    bool saw_topic{};
    for (int index = 0; index < 2; ++index) {
        auto data = receive_frame();
        zassert_true(data.has_value());
        zassert_equal(data->envelope.kind, solar::remote::protocol::Kind::Data);
        auto value = solar::remote::cbor::decode<fixture::Change>(data->payload);
        zassert_true(value.has_value());
        if (data->envelope.subscription() == solar::remote::protocol::SubscriptionKind::DataWatch) {
            saw_watch = true;
            zassert_equal(value->sequence, 11);
        } else if (data->envelope.subscription() ==
                   solar::remote::protocol::SubscriptionKind::Topic) {
            saw_topic = true;
            zassert_equal(value->sequence, 22);
        }
    }
    zassert_true(saw_watch);
    zassert_true(saw_topic);

    unsubscribe(solar::remote::protocol::SubscriptionKind::DataWatch, 3);
    zassert_false(solar::remote::interested<fixture::Watched>());
    zassert_true(solar::remote::interested<fixture::Notice>());
    auto ignored = solar::remote::publish<fixture::Watched>({.sequence = 33});
    zassert_true(ignored.has_value());
    zassert_equal(ignored->disposition, solar::remote::WriteDisposition::NoSubscribers);

    zassert_true(fixture::System::stop().has_value());
}

ZTEST_SUITE(remote_topics, nullptr, nullptr, nullptr, nullptr, nullptr);
