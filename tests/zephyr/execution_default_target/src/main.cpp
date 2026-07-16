#include <atomic>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <solar/solar.hpp>

namespace fixture
{

inline std::atomic_uint runs{};

struct Behavior
{
    static void execute()
    {
        ++runs;
    }
};

using DefaultTask = solar::execution::OnDemand<"default-task", Behavior>;
using System = solar::System<solar::Blueprint<solar::Execution<DefaultTask>>>;

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

ZTEST(solar_execution_default, test_omitted_target_resolves_to_system_queue)
{
    auto boot = solar::boot();
    zassert_true(boot.has_value());
    auto submitted = solar::execution::submit<fixture::DefaultTask>();
    zassert_true(submitted.has_value());
    zassert_equal(submitted->target_kind, solar::execution::TargetKind::SystemWorkQueue);
    for (int attempt = 0; attempt < 100 && fixture::runs.load(std::memory_order_acquire) == 0;
         ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_equal(fixture::runs.load(std::memory_order_acquire), 1);
    const auto record = solar::execution::registration<fixture::DefaultTask>();
    zassert_true(record.has_value());
    zassert_equal(record->target_source, solar::execution::TargetSource::KconfigDefault);
    zassert_true(solar::stop().has_value());
}

ZTEST_SUITE(solar_execution_default, nullptr, nullptr, nullptr, nullptr, nullptr);
