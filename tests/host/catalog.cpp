#include <array>
#include <cassert>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

#include "catalog_fixture.hpp"

namespace fixture = catalog_fixture;

namespace
{

struct BuiltinMetric;
struct BuiltinEvent;
struct BuiltinMessage;
struct BuiltinParameter;
struct BuiltinAction;
struct BuiltinCheck;

struct BuiltinAliasOwner
{
    using Metrics = solar::metrics::Contribute<BuiltinMetric>;
    using Events = solar::events::Contribute<BuiltinEvent>;
    using Messages = solar::bus::ContributeMessages<BuiltinMessage>;
    using Parameters = solar::parameters::Contribute<BuiltinParameter>;
    using RemoteActions = solar::remote::ContributeActions<BuiltinAction>;
    struct Health
    {
        using Checks = solar::health::Checks<BuiltinCheck>;
    };
};

template <std::size_t Value> consteval auto large_name()
{
    std::array<char, 12> text{'l', 'a', 'r', 'g', 'e', '.'};
    auto remaining = Value;
    std::size_t digits = 1;
    for (auto copy = remaining; copy >= 10; copy /= 10) {
        ++digits;
    }
    for (std::size_t offset = 0; offset < digits; ++offset) {
        text[6 + digits - offset - 1] = static_cast<char>('0' + remaining % 10);
        remaining /= 10;
    }
    return text;
}

template <std::size_t Index> struct LargeEntry
{
    inline static constexpr auto name = large_name<Index>();
    static constexpr fixture::alpha::Descriptor descriptor{
        .name = std::string_view{name.data(), 6 + (Index < 10 ? 1U : 2U)},
        .stable_id = fixture::alpha::Id{static_cast<std::uint32_t>(0x4000 + Index)},
    };
};

template <typename Sequence> struct LargeCatalog;

template <std::size_t... Indices> struct LargeCatalog<std::index_sequence<Indices...>>
{
    using type = solar::collect_catalog_t<
        fixture::alpha::Tag, solar::DirectDeclarations<fixture::alpha::Tag, LargeEntry<Indices>...>,
        solar::TypeList<>>;
};

using RepresentativeLargeCatalog = typename LargeCatalog<std::make_index_sequence<64>>::type;

static_assert(std::is_same_v<
              solar::detail::contributed_declarations_t<solar::metrics::Tag, BuiltinAliasOwner>,
              solar::TypeList<BuiltinMetric>>);
static_assert(
    std::is_same_v<solar::detail::contributed_declarations_t<solar::events::Tag, BuiltinAliasOwner>,
                   solar::TypeList<BuiltinEvent>>);
static_assert(std::is_same_v<
              solar::detail::contributed_declarations_t<solar::bus::MessageTag, BuiltinAliasOwner>,
              solar::TypeList<BuiltinMessage>>);
static_assert(std::is_same_v<
              solar::detail::contributed_declarations_t<solar::parameters::Tag, BuiltinAliasOwner>,
              solar::TypeList<BuiltinParameter>>);
static_assert(std::is_same_v<solar::detail::contributed_declarations_t<solar::remote::ActionTag,
                                                                       BuiltinAliasOwner>,
                             solar::TypeList<BuiltinAction>>);
static_assert(std::is_same_v<
              solar::detail::contributed_declarations_t<solar::health::CheckTag, BuiltinAliasOwner>,
              solar::TypeList<solar::health::OwnedMonitor<BuiltinAliasOwner, BuiltinCheck>>>);

} // namespace

static_assert(fixture::ComponentCatalog::size == 2);
static_assert(fixture::AlphaCatalog::size == 6);
static_assert(fixture::BetaCatalog::size == 1);

static_assert(fixture::AlphaCatalog::contains<fixture::AlphaDirect>);
static_assert(fixture::AlphaCatalog::contains<fixture::AlphaExternal>);
static_assert(fixture::AlphaCatalog::contains<fixture::AlphaExpandedA>);
static_assert(!fixture::AlphaCatalog::contains<fixture::AlphaGroup>);

static_assert(fixture::AlphaCatalog::Entry<fixture::AlphaDirect>::local_id.value == 0);
static_assert(fixture::AlphaCatalog::Entry<fixture::AlphaExternal>::local_id.value == 1);
static_assert(fixture::AlphaCatalog::Entry<fixture::AlphaOwned>::local_id.value == 2);
static_assert(fixture::AlphaCatalog::Entry<fixture::AlphaExpandedA>::local_id.value == 3);
static_assert(fixture::AlphaCatalog::Entry<fixture::AlphaExpandedB>::local_id.value == 4);
static_assert(fixture::AlphaCatalog::Entry<fixture::AlphaGeneric>::local_id.value == 5);

static_assert(
    std::is_same_v<fixture::AlphaCatalog::Entry<fixture::AlphaOwned>::Owner, fixture::OwnerA>);
static_assert(std::is_same_v<fixture::AlphaCatalog::Entry<fixture::AlphaOwned>::Origin,
                             solar::origin::Contribution<fixture::OwnerA>>);
static_assert(std::is_same_v<fixture::AlphaCatalog::Entry<fixture::AlphaExpandedA>::Origin,
                             solar::origin::Expansion<solar::origin::Contribution<fixture::OwnerA>,
                                                      fixture::AlphaGroup>>);
static_assert(fixture::AlphaCatalog::Entry<fixture::AlphaOwned>::owner_id.value == 0);
static_assert(fixture::AlphaCatalog::Entry<fixture::AlphaDirect>::owner_view().kind ==
              solar::OwnerKind::Application);
static_assert(fixture::AlphaCatalog::Entry<fixture::AlphaOwned>::owner_view().kind ==
              solar::OwnerKind::Component);

static_assert(solar::list_size_v<fixture::AlphaOwnedEntries> == 4);
static_assert(
    std::is_same_v<typename fixture::ResolvedBetaSource::Declaration, fixture::AlphaOwned>);
static_assert(!std::is_same_v<fixture::alpha::Id, fixture::beta::Id>);
static_assert(!std::is_convertible_v<solar::LocalId<fixture::alpha::Tag>,
                                     solar::LocalId<fixture::beta::Tag>>);
static_assert(
    std::is_same_v<typename solar::LocalId<fixture::alpha::Tag>::Representation, std::uint16_t>);
static_assert(RepresentativeLargeCatalog::size == 64);

int main()
{
    const auto descriptors = fixture::AlphaCatalog::descriptors();
    assert(descriptors.size() == fixture::AlphaCatalog::size);
    assert(descriptors[0].descriptor.name == "alpha.direct");
    assert(descriptors[1].descriptor.description == "Descriptor supplied by trait specialization");
    assert(descriptors[2].owner.kind == solar::OwnerKind::Component);
    assert(descriptors[2].owner.component.value == 0);
    assert(descriptors[3].origin == solar::OriginKind::Expansion);

    const auto by_local = fixture::AlphaCatalog::find(solar::LocalId<fixture::alpha::Tag>{2});
    assert(by_local && by_local->get().descriptor.name == "alpha.owned");

    const auto by_stable = fixture::AlphaCatalog::find(fixture::alpha_direct_id);
    assert(by_stable && by_stable->get().local_id.value == 0);

    const auto missing_local = fixture::AlphaCatalog::find(solar::LocalId<fixture::alpha::Tag>{99});
    assert(!missing_local && missing_local.error() == solar::catalog::LookupError::UnknownLocalId);

    const auto missing_stable = fixture::AlphaCatalog::find(fixture::alpha::Id{0xFFFF});
    assert(!missing_stable &&
           missing_stable.error() == solar::catalog::LookupError::UnknownStableId);

    assert(descriptors.data() == fixture::descriptor_data_from_other_translation_unit());
    return 0;
}
