#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <solar/remote.hpp>
#include <solar/remote/testing/in_memory_link.hpp>
#include <solar/solar.hpp>

namespace app
{

// [types]
struct TelemetrySample
{
    std::uint32_t sequence{};
    float value{};
};

struct ScaleRequest
{
    std::int32_t value{};
};
struct ScaleResponse
{
    std::int32_t value{};
};
// [types]

// [endpoints]
struct Telemetry
{
    using Value = TelemetrySample;
    static constexpr solar::remote::DataDescriptor descriptor{.id = solar::remote::DataId{0x7101},
                                                              .name = "demo.telemetry"};

    static Value read()
    {
        return {.sequence = 7, .value = 2.5F};
    }

    using Capabilities = solar::remote::Capabilities<
        solar::remote::Query<&Telemetry::read>,
        solar::remote::OutStream<solar::remote::Push, solar::remote::Latest,
                                 solar::remote::MaxRate<100>>>;
};

struct Scale
{
    using Request = ScaleRequest;
    using Response = ScaleResponse;
    static constexpr solar::remote::ActionDescriptor descriptor{
        .id = solar::remote::ActionId{0x7301}, .name = "demo.scale"};
    using Execution = solar::remote::Inline;

    static Response execute(const Request& request)
    {
        return {.value = request.value * 2};
    }
};
// [endpoints]

struct Link : solar::remote::testing::InMemoryLink<Link, 256, 256>
{
    static constexpr solar::remote::LinkDescriptor descriptor{.id = solar::remote::LinkId{0x7001},
                                                              .name = "demo.memory"};
    using Grants = solar::remote::Requires<solar::remote::permission::Observe,
                                           solar::remote::permission::Control>;
};

struct Root
{
    static constexpr solar::component::Descriptor descriptor{.name = "remote-demo"};
    using RemoteData = solar::remote::ContributeData<Telemetry>;
    using RemoteActions = solar::remote::ContributeActions<Scale>;
    using RemoteLinks = solar::remote::ContributeLinks<Link>;
};

using System = solar::System<solar::Blueprint<solar::Facilities<Root>>>;

} // namespace app

template <> struct solar::remote::Schema<app::TelemetrySample>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{0x7201},
                                                 .name = "demo.TelemetrySample"};
    using Fields = remote::Fields<Field<1, "sequence", &app::TelemetrySample::sequence>,
                                  Field<2, "value", &app::TelemetrySample::value>>;
    static constexpr std::size_t max_encoded_size = 24;
    static constexpr Codec codec = Codec::Cbor;
};

template <> struct solar::remote::Schema<app::ScaleRequest>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{0x7202}, .name = "demo.ScaleRequest"};
    using Fields = remote::Fields<Field<1, "value", &app::ScaleRequest::value>>;
    static constexpr std::size_t max_encoded_size = 12;
    static constexpr Codec codec = Codec::Cbor;
};

template <> struct solar::remote::Schema<app::ScaleResponse>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{0x7203},
                                                 .name = "demo.ScaleResponse"};
    using Fields = remote::Fields<Field<1, "value", &app::ScaleResponse::value>>;
    static constexpr std::size_t max_encoded_size = 12;
    static constexpr Codec codec = Codec::Cbor;
};

SOLAR_BIND_SYSTEM(app::System);

int main()
{
    if (!solar::boot() || !app::Link::opened() || !app::Link::connect()) {
        return 1;
    }

    std::array<std::byte, 256> bytes{};
    solar::Result<std::size_t, solar::remote::LinkError> hello =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !hello; ++attempt) {
        hello = app::Link::take_transmitted(bytes);
        k_sleep(K_MSEC(1));
    }
    if (!hello) {
        return 2;
    }

    std::array<std::byte, 256> scratch{};
    auto frame = solar::remote::frame::decode(std::span{bytes}.first(*hello), scratch);
    if (!frame || frame->envelope.kind != solar::remote::protocol::Kind::ServerHello) {
        return 3;
    }

    if (!solar::remote::write<app::Telemetry>({.sequence = 8, .value = 3.0F})) {
        return 4;
    }
    if (!solar::stop()) {
        return 5;
    }
    printk("Solar Remote control passed\n");
    return 0;
}
