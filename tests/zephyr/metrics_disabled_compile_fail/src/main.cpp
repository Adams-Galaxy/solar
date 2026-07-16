#include <solar/metrics.hpp>
#include <solar/solar.hpp>

namespace
{

struct Metric
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    static constexpr solar::metrics::Descriptor descriptor{.name = "disabled.metric"};
};

using InvalidSystem = solar::System<solar::Blueprint<solar::Metrics<Metric>>>;
static_assert(InvalidSystem::valid);

} // namespace

int main()
{
    return 0;
}
