#include <solar/solar.hpp>

namespace
{

struct First
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    static constexpr solar::metrics::Descriptor descriptor{.name = "failure.first"};
};

struct Second : First
{
    static constexpr solar::metrics::Descriptor descriptor{.name = "failure.second"};
};

#if SOLAR_FAIL_CASE == 1
struct Invalid : First
{
    struct InvalidUnit
    {};
    using Unit = InvalidUnit;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<Invalid>>>;

#elif SOLAR_FAIL_CASE == 2
struct Invalid : First
{
    using Value = std::uint64_t;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<Invalid>>>;

#elif SOLAR_FAIL_CASE == 3
struct Invalid : First
{
    using Value = std::int32_t;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<Invalid>>>;

#elif SOLAR_FAIL_CASE == 4
struct Invalid : First
{
    using Instrument = solar::metrics::Distribution<solar::metrics::Summary>;
    using Concurrency = solar::metrics::concurrency::Atomic;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<Invalid>>>;

#elif SOLAR_FAIL_CASE == 5
struct Invalid : First
{
    using Concurrency = solar::metrics::concurrency::Atomic;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<Invalid>>>;

#elif SOLAR_FAIL_CASE == 6
struct Invalid : First
{
    using Instrument = solar::metrics::Gauge;
    using Concurrency = solar::metrics::concurrency::SpinLocked;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<Invalid>>>;

#elif SOLAR_FAIL_CASE == 7
struct Invalid : First
{
    using Instrument = solar::metrics::Distribution<solar::metrics::Summary>;
    using Concurrency = solar::metrics::concurrency::MutexProtected;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<Invalid>>>;

#elif SOLAR_FAIL_CASE == 8
struct Invalid : First
{
    using Reset = solar::metrics::RuntimeResettable;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<Invalid>>>;

#elif SOLAR_FAIL_CASE == 9
struct Invalid : First
{
    using Timestamps = solar::metrics::timestamps::Enabled;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<Invalid>>>;

#elif SOLAR_FAIL_CASE == 10
struct Invalid : First
{
    using Instrument = solar::metrics::Distribution<solar::metrics::WindowMean<3>>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<Invalid>>>;

#elif SOLAR_FAIL_CASE == 11
struct Invalid : First
{
    using Instrument = solar::metrics::Distribution<solar::metrics::Histogram<1U, 2U, 3U>>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<Invalid>>>;

#elif SOLAR_FAIL_CASE == 12
struct Invalid : First
{
    using Instrument = solar::metrics::Timer<>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<Invalid>>>;

#elif SOLAR_FAIL_CASE == 13
struct Invalid : First
{
    using Instrument = solar::metrics::Distribution<solar::metrics::Histogram<2U, 1U>>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<Invalid>>>;

#elif SOLAR_FAIL_CASE == 14 || SOLAR_FAIL_CASE == 15 || SOLAR_FAIL_CASE == 16 ||                   \
    SOLAR_FAIL_CASE == 17
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<First>>>;

#elif SOLAR_FAIL_CASE == 18
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<First, Second>>>;

#elif SOLAR_FAIL_CASE == 19
struct Invalid : First
{
    using Value = std::int32_t;
    using Instrument = solar::metrics::Distribution<solar::metrics::Summary>;
    using Concurrency = solar::metrics::concurrency::MutexProtected;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<Invalid>>>;

#else
#error SOLAR_DIAGNOSTIC_UNKNOWN_METRICS_FAILURE_CASE
#endif

} // namespace

#if (SOLAR_FAIL_CASE >= 14 && SOLAR_FAIL_CASE <= 17) || SOLAR_FAIL_CASE == 19
SOLAR_BIND_SYSTEM(InvalidSystem);
#endif

int main()
{
#if SOLAR_FAIL_CASE == 14
    (void)solar::metrics::inc<Second>();
#elif SOLAR_FAIL_CASE == 15
    (void)solar::metrics::set<First>(1);
#elif SOLAR_FAIL_CASE == 16
    (void)solar::metrics::get_view<First, solar::metrics::view::Mean>();
#elif SOLAR_FAIL_CASE == 17
    (void)solar::metrics::reset<First>();
#elif SOLAR_FAIL_CASE == 19
    (void)solar::metrics::try_observe_isr<Invalid>(1);
#else
    static_assert(InvalidSystem::valid);
#endif
    return 0;
}
