#include <array>
#include <cassert>
#include <cstddef>

#include <solar/system/composer.hpp>

namespace fixture
{

inline std::array<int, 32> calls{};
inline std::size_t call_count{};

inline void record(int value)
{
    calls[call_count++] = value;
}

struct Foundation
{
    using Dependencies = solar::TypeList<>;
    struct CatalogEntry
    {};
    struct Capability
    {};
    struct Inspector
    {};
    using Catalog = solar::TypeList<CatalogEntry>;
    using Capabilities = solar::TypeList<Capability>;
    using Inspection = solar::TypeList<Inspector>;
    static solar::Result<void> initialize() noexcept
    {
        record(1);
        return {};
    }
    static solar::Result<void> start() noexcept
    {
        record(5);
        return {};
    }
    static solar::Result<void> stop() noexcept
    {
        record(10);
        return {};
    }
    static solar::Result<void> deinitialize() noexcept
    {
        record(14);
        return {};
    }
};

struct Feature
{
    using Dependencies = solar::TypeList<Foundation>;
    static solar::Result<void> initialize() noexcept
    {
        record(2);
        return {};
    }
    static solar::Result<void> start() noexcept
    {
        record(6);
        return {};
    }
    static solar::Result<void> stop() noexcept
    {
        record(9);
        return {};
    }
    static solar::Result<void> deinitialize() noexcept
    {
        record(13);
        return {};
    }
};

struct Application;

struct Adapter
{
    template <typename Left, typename Right> static solar::Result<void> connect() noexcept
    {
        static_assert(std::same_as<Left, Foundation> && std::same_as<Right, Feature>);
        record(3);
        return {};
    }

    template <typename Left, typename Right> static solar::Result<void> disconnect() noexcept
    {
        record(11);
        return {};
    }
};

using AppSystem = solar::system::System<
    Application,
    solar::Compose<solar::Own<Feature, Foundation>, solar::Connect<Adapter, Foundation, Feature>>>;

struct FailingFeature
{
    using Dependencies = solar::TypeList<Foundation>;
    static solar::Result<void> initialize() noexcept
    {
        record(2);
        return {};
    }
    static solar::Result<void> start() noexcept
    {
        record(6);
        return solar::fail<solar::Error>({.status = solar::Status::Error});
    }
    static solar::Result<void> deinitialize() noexcept
    {
        record(13);
        return {};
    }
};

struct FailingAdapter
{
    template <typename...> static solar::Result<void> connect() noexcept
    {
        record(3);
        return {};
    }
    template <typename...> static solar::Result<void> disconnect() noexcept
    {
        record(11);
        return {};
    }
};

using FailingSystem = solar::system::System<
    struct FailureApplication,
    solar::Compose<solar::Own<Foundation, FailingFeature>,
                   solar::Connect<FailingAdapter, Foundation, FailingFeature>>>;

} // namespace fixture

int main()
{
    using namespace fixture;
    call_count = 0;
    assert(AppSystem::boot());
    static_assert(solar::contains_v<Foundation::CatalogEntry, AppSystem::Catalogs>);
    static_assert(solar::contains_v<Foundation::Capability, AppSystem::Capabilities>);
    static_assert(solar::contains_v<Foundation::Inspector, AppSystem::Inspection>);
    assert(AppSystem::active());
    assert(AppSystem::stage() == solar::system::LifecycleStage::Active);
    assert(AppSystem::shutdown());
    assert(AppSystem::shutdown());
    assert(AppSystem::stage() == solar::system::LifecycleStage::Idle);
    const std::array expected{1, 2, 3, 5, 6, 9, 10, 11, 13, 14};
    assert(call_count == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        assert(calls[index] == expected[index]);
    }

    call_count = 0;
    assert(!FailingSystem::boot());
    assert(!FailingSystem::active());
    assert(FailingSystem::stage() == solar::system::LifecycleStage::Failed);
    const std::array rollback{1, 2, 3, 5, 6, 10, 11, 13, 14};
    assert(call_count == rollback.size());
    for (std::size_t index = 0; index < rollback.size(); ++index) {
        assert(calls[index] == rollback[index]);
    }
}
