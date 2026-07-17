#include <array>
#include <cstdint>
#include <span>

#include <solar/solar.hpp>

namespace
{

struct Store
{
    [[nodiscard]] static solar::Result<void> initialize()
    {
        return {};
    }
    [[nodiscard]] static solar::Result<std::size_t> load(solar::parameters::persistence::Key,
                                                         std::span<std::byte>)
    {
        return solar::fail<solar::Error>({.status = solar::Status::NotFound});
    }
    [[nodiscard]] static solar::Result<void> save(solar::parameters::persistence::Key,
                                                  std::span<const std::byte>)
    {
        return {};
    }
    [[nodiscard]] static solar::Result<void> erase(solar::parameters::persistence::Key)
    {
        return {};
    }
};

struct OtherStore : Store
{};

struct First
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "failure.first",
        .stable_id = solar::parameters::Id{0xA001},
    };
    static constexpr Value default_value = 1;
};

struct Second
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "failure.second",
        .stable_id = solar::parameters::Id{0xA002},
    };
    static constexpr Value default_value = 2;
};

struct Third
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{.name = "failure.third"};
    static constexpr Value default_value = 3;
};

#if SOLAR_FAIL_CASE == 1
struct Invalid
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{.name = "failure.default"};
    static constexpr Value default_value = 20;
    using Validation = solar::parameters::Range<0, 10>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Parameters<Invalid>>>;

#elif SOLAR_FAIL_CASE == 2
struct ReadOnly
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{.name = "failure.read-only"};
    static constexpr Value default_value = 1;
    using Access = solar::parameters::ReadOnly;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Parameters<ReadOnly>>>;

#elif SOLAR_FAIL_CASE == 3
struct Authority
{};
struct Privileged
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{.name = "failure.privileged"};
    static constexpr Value default_value = 1;
    using Access = solar::parameters::Privileged<Authority>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Parameters<Privileged>>>;

#elif SOLAR_FAIL_CASE == 4
struct WideValue
{
    std::uint64_t first{};
    std::uint64_t second{};
    friend bool operator==(const WideValue&, const WideValue&) = default;
};
struct Invalid
{
    using Value = WideValue;
    static constexpr solar::parameters::Descriptor descriptor{.name = "failure.atomic"};
    static constexpr Value default_value{};
    using Storage = solar::parameters::Atomic;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Parameters<Invalid>>>;

#elif SOLAR_FAIL_CASE == 5
struct Invalid
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "failure.persistence-disabled",
        .stable_id = solar::parameters::Id{0xA005},
    };
    static constexpr Value default_value = 1;
    using Persistence = solar::parameters::Manual<Store>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Parameters<Invalid>>>;

#elif SOLAR_FAIL_CASE == 6
struct Invalid
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{.name = "failure.stable-id"};
    static constexpr Value default_value = 1;
    using Persistence = solar::parameters::Manual<Store>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Parameters<Invalid>>>;

#elif SOLAR_FAIL_CASE == 7
struct Unencoded
{
    int value{};
    friend bool operator==(const Unencoded&, const Unencoded&) = default;
};
struct Invalid
{
    using Value = Unencoded;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "failure.codec",
        .stable_id = solar::parameters::Id{0xA007},
    };
    static constexpr Value default_value{};
    using Persistence = solar::parameters::Manual<Store>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Parameters<Invalid>>>;

#elif SOLAR_FAIL_CASE == 8
using InvalidSystem = solar::System<solar::Blueprint<solar::Parameters<First>>>;

#elif SOLAR_FAIL_CASE == 9
struct Observer
{
    static constexpr solar::component::Descriptor descriptor{.name = "observer"};
    using ParameterChanges = solar::parameters::Changes<First>;
    static void changed(const solar::parameters::Change<First>&) {}
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<Observer>>>;

#elif SOLAR_FAIL_CASE == 10
struct Observer
{
    static constexpr solar::component::Descriptor descriptor{.name = "observer"};
    using Parameters = solar::parameters::Parameters<First>;
    using ParameterChanges = solar::parameters::Changes<First>;
    static bool changed(const solar::parameters::Change<First>&)
    {
        return true;
    }
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<Observer>>>;

#elif SOLAR_FAIL_CASE == 11
struct RouteTag
{};
struct FirstHandler
{
    static void changed(const solar::parameters::Change<First>&) {}
};
struct SecondHandler
{
    static void changed(const solar::parameters::Change<First>&) {}
};
struct Observer
{
    static constexpr solar::component::Descriptor descriptor{.name = "observer"};
    using Parameters = solar::parameters::Parameters<First>;
    using ParameterChanges =
        solar::parameters::Changes<solar::parameters::ChangeRoute<RouteTag, First, FirstHandler>,
                                   solar::parameters::ChangeRoute<RouteTag, First, SecondHandler>>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<Observer>>>;

#elif SOLAR_FAIL_CASE == 12
struct MissingGroup;
struct Invalid
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "failure.missing-group",
        .stable_id = solar::parameters::Id{0xA012},
    };
    static constexpr Value default_value = 1;
    using Persistence = solar::parameters::Transactional<MissingGroup>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Parameters<Invalid>>>;

#elif SOLAR_FAIL_CASE == 13
struct Group;
struct Member
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "failure.group-store",
        .stable_id = solar::parameters::Id{0xA013},
    };
    static constexpr Value default_value = 1;
    using Persistence = solar::parameters::Transactional<Group>;
};
struct Group
{
    using Members = solar::parameters::Members<Member>;
    using Store = Store;
    using Commit = solar::parameters::Manual<OtherStore>;
    static constexpr solar::parameters::GroupId stable_id{0xB013};
    static constexpr std::uint16_t version = 1;
};
using InvalidSystem = solar::System<solar::Blueprint<
    solar::Parameters<Member>,
    solar::parameters::Configuration<solar::parameters::PersistenceGroups<Group>>>>;

#elif SOLAR_FAIL_CASE == 14
struct ImmediateFirst : First
{
    using Persistence = solar::parameters::Immediate<Store>;
};
struct ImmediateSecond : Second
{
    using Persistence = solar::parameters::Immediate<Store>;
};
using InvalidSystem =
    solar::System<solar::Blueprint<solar::Parameters<ImmediateFirst, ImmediateSecond>>>;

#elif SOLAR_FAIL_CASE == 15
using InvalidSystem = solar::System<solar::Blueprint<solar::Parameters<First>>>;

#elif SOLAR_FAIL_CASE == 16
using InvalidSystem = solar::System<solar::Blueprint<solar::Parameters<First, Second, Third>>>;

#elif SOLAR_FAIL_CASE == 17
struct BadValidator
{
    static bool normalize(int)
    {
        return true;
    }
};
struct Invalid
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{.name = "failure.validator"};
    static constexpr Value default_value = 1;
    using Validation = solar::parameters::Custom<BadValidator>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Parameters<Invalid>>>;

#else
#error SOLAR_DIAGNOSTIC_UNKNOWN_PARAMETERS_FAILURE_CASE
#endif

} // namespace

#if SOLAR_FAIL_CASE == 2 || SOLAR_FAIL_CASE == 3 || SOLAR_FAIL_CASE == 8 ||                        \
    SOLAR_FAIL_CASE == 14 || SOLAR_FAIL_CASE == 15
SOLAR_BIND_SYSTEM(InvalidSystem);
#endif

int main()
{
#if SOLAR_FAIL_CASE == 2
    (void)solar::parameters::set<ReadOnly>(2);
#elif SOLAR_FAIL_CASE == 3
    (void)solar::parameters::set<Privileged>(2);
#elif SOLAR_FAIL_CASE == 8
    (void)solar::parameters::set_all(solar::parameters::assign<First>(1),
                                     solar::parameters::assign<First>(2));
#elif SOLAR_FAIL_CASE == 14
    (void)solar::parameters::set_all(solar::parameters::assign<ImmediateFirst>(1),
                                     solar::parameters::assign<ImmediateSecond>(2));
#elif SOLAR_FAIL_CASE == 15
    (void)solar::parameters::get<Second>();
#else
    static_assert(InvalidSystem::valid);
#endif
    return 0;
}
