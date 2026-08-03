#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>

#include <solar/parameters/persistence_adapter.hpp>
#include <solar/parameters/store.hpp>
#include <solar/persistence/storage.hpp>

namespace fixture
{

struct Gain
{
    using Value = float;
    static constexpr const char* name = "drive.gain";
    static constexpr Value default_value = 1.0F;
    static constexpr Value minimum = 0.0F;
    static constexpr Value maximum = 10.0F;
    static constexpr std::uint32_t id = 1;
};

struct Limit
{
    using Value = int;
    static constexpr const char* name = "drive.limit";
    static constexpr Value default_value = 5;
    static constexpr Value minimum = 0;
    static constexpr Value maximum = 100;
    static constexpr std::uint32_t id = 2;
};

using Schema = solar::parameters::Schema<Gain, Limit>;
struct ApplicationA;
struct ApplicationB;
using ParametersA = solar::parameters::StaticStore<ApplicationA, Schema>;
using ParametersB = solar::parameters::StaticStore<ApplicationB, Schema>;

struct Codec
{
    template <typename Parameter>
    static solar::Result<std::size_t> encode(typename Parameter::Value value,
                                             std::span<std::byte> output) noexcept
    {
        if (output.size() < sizeof(value))
            return solar::fail<solar::Error>({.status = solar::Status::NoSpace});
        const auto bytes = std::bit_cast<std::array<std::byte, sizeof(value)>>(value);
        std::copy(bytes.begin(), bytes.end(), output.begin());
        return sizeof(value);
    }
    template <typename Parameter>
    static solar::Result<typename Parameter::Value>
    decode(std::span<const std::byte> input) noexcept
    {
        using Value = typename Parameter::Value;
        if (input.size() != sizeof(Value))
            return solar::fail<solar::Error>({.status = solar::Status::ProtocolError});
        std::array<std::byte, sizeof(Value)> bytes{};
        std::copy(input.begin(), input.end(), bytes.begin());
        return std::bit_cast<Value>(bytes);
    }
};

template <typename Parameters> void exercise()
{
    assert(Parameters::initialize());
    assert(*Parameters::template get<Gain>() == 1.0F);
    assert(Parameters::template set<Gain>(2.5F));
    assert(*Parameters::template get<Gain>() == 2.5F);
    assert(*Parameters::template revision<Gain>() == 1);

    const auto transaction = Parameters::set_many(solar::parameters::ValueAssignment<Gain>{3.5F},
                                                  solar::parameters::ValueAssignment<Limit>{25});
    assert(transaction && transaction->updated == 2);
    assert(*Parameters::template get<Gain>() == 3.5F);
    assert(*Parameters::template get<Limit>() == 25);

    const auto rejected = Parameters::set_many(solar::parameters::ValueAssignment<Gain>{4.5F},
                                               solar::parameters::ValueAssignment<Limit>{101});
    assert(!rejected);
    assert(*Parameters::template get<Gain>() == 3.5F);
    assert(*Parameters::template get<Limit>() == 25);
}

} // namespace fixture

int main()
{
    using namespace fixture;

    solar::parameters::Store<Schema> first;
    solar::parameters::Store<Schema> second;
    assert(first.initialize());
    assert(second.initialize());
    assert(first.set<Gain>(7.0F));
    assert(*first.get<Gain>() == 7.0F);
    assert(*second.get<Gain>() == 1.0F);

    exercise<ParametersA>();
    assert(ParametersB::initialize());
    assert(ParametersB::set<Gain>(8.0F));
    assert(*ParametersA::get<Gain>() == 3.5F);
    assert(*ParametersB::get<Gain>() == 8.0F);

    const auto snapshot = ParametersA::snapshot<Gain, Limit>();
    assert(snapshot && snapshot->get<Gain>() == 3.5F && snapshot->get<Limit>() == 25);
    bool visited{};
    assert(ParametersA::visit(
        1, [&]<typename Declaration>(Declaration, const auto& value, std::uint64_t revision) {
            if constexpr (std::is_same_v<Declaration, Gain>) {
                visited = value == 3.5F && revision == 2;
            }
        }));
    assert(visited);
    assert(!ParametersA::visit(99, [](auto, const auto&, std::uint64_t) {}));

    solar::parameters::Store<Schema> concurrent;
    assert(concurrent.initialize());
    std::vector<std::thread> writers;
    for (int worker = 0; worker < 4; ++worker) {
        writers.emplace_back([&, worker] {
            for (int iteration = 0; iteration < 250; ++iteration) {
                assert(concurrent.set<Limit>(worker * 10 + iteration % 10));
            }
        });
    }
    for (auto& writer : writers)
        writer.join();
    assert(*concurrent.revision<Limit>() == 1000);

    solar::persistence::MemoryStorage<2, 32> persisted;
    assert(persisted.initialize());
    solar::parameters::PersistenceAdapter<ParametersA, decltype(persisted), Codec, 8> adapter{
        persisted};
    assert(adapter.save<Gain>());
    assert(ParametersA::set<Gain>(1.0F));
    assert(adapter.load<Gain>());
    assert(*ParametersA::get<Gain>() == 3.5F);

    std::array<std::byte, 32> corrupted{};
    const auto persisted_size = persisted.read(Gain::id, corrupted);
    assert(persisted_size && *persisted_size > 12);
    corrupted[12] ^= std::byte{0x01};
    assert(persisted.write(Gain::id, std::span{corrupted}.first(*persisted_size)));
    assert(!adapter.load<Gain>());

    assert(adapter.save<Gain>());
    solar::parameters::PersistenceAdapter<ParametersA, decltype(persisted), Codec, 8>
        incompatible_version{persisted, 2};
    assert(!incompatible_version.load<Gain>());

    static_assert(!std::is_copy_constructible_v<solar::parameters::Store<Schema>>);
}
