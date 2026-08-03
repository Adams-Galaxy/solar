#include <type_traits>

#include <solar/application.hpp>
#include <solar/kernel/priority.hpp>

namespace kernel = solar::kernel;

namespace
{
struct Application;
struct Service
{};
#if CONFIG_NUM_PREEMPT_PRIORITIES > 0
using PreemptiveRunner =
    solar::execution::ServiceRunner<Application, Service, 2048, solar::PreemptivePriority<0>>;
static_assert(PreemptiveRunner::stack_size == 2048);
static_assert(PreemptiveRunner::scheduled_priority().native_handle() == K_PRIO_PREEMPT(0));
static_assert(PreemptiveRunner::scheduled_priority().is_preemptive());
static_assert(PreemptiveRunner::scheduled_priority().level() == 0);
static_assert(PreemptiveRunner::scheduling().priority_class == kernel::PriorityClass::Preemptive);
static_assert(PreemptiveRunner::scheduling().native_priority == K_PRIO_PREEMPT(0));
#endif
#if CONFIG_NUM_COOP_PRIORITIES > 0
using CooperativeRunner =
    solar::execution::ServiceRunner<Application, Service, 3072, solar::CooperativePriority<0>>;
static_assert(CooperativeRunner::stack_size == 3072);
static_assert(CooperativeRunner::scheduled_priority().native_handle() == K_PRIO_COOP(0));
static_assert(CooperativeRunner::scheduled_priority().is_cooperative());
static_assert(CooperativeRunner::scheduled_priority().level() == 0);
static_assert(CooperativeRunner::scheduling().priority_class == kernel::PriorityClass::Cooperative);
static_assert(CooperativeRunner::scheduling().native_priority == K_PRIO_COOP(0));
#endif
} // namespace

static_assert(!std::is_default_constructible_v<kernel::Priority>);
static_assert(kernel::Priority::native<K_HIGHEST_APPLICATION_THREAD_PRIO>().native_handle() ==
              K_HIGHEST_APPLICATION_THREAD_PRIO);
static_assert(kernel::Priority::native<K_LOWEST_APPLICATION_THREAD_PRIO>().native_handle() ==
              K_LOWEST_APPLICATION_THREAD_PRIO);

#if CONFIG_NUM_PREEMPT_PRIORITIES > 0
static_assert(kernel::Priority::preemptive<0>().native_handle() == K_PRIO_PREEMPT(0));
static_assert(kernel::Priority::preemptive<0>().is_preemptive());
static_assert(solar::PreemptivePriority<0>::resolve() == kernel::Priority::preemptive<0>());
#endif

#if CONFIG_NUM_COOP_PRIORITIES > 0
static_assert(kernel::Priority::cooperative<0>().native_handle() == K_PRIO_COOP(0));
static_assert(kernel::Priority::cooperative<0>().is_cooperative());
static_assert(solar::CooperativePriority<0>::resolve() == kernel::Priority::cooperative<0>());
#endif

static_assert(solar::Priority<K_HIGHEST_APPLICATION_THREAD_PRIO>::resolve() ==
              kernel::Priority::native<K_HIGHEST_APPLICATION_THREAD_PRIO>());

constexpr auto default_service_priority =
    solar::application::detail::DefaultServicePriority::resolve();
#if defined(CONFIG_SOLAR_APPLICATION_DEFAULT_SERVICE_PRIORITY_COOPERATIVE)
static_assert(default_service_priority.is_cooperative());
#else
static_assert(default_service_priority.is_preemptive());
#endif
static_assert(default_service_priority.level() ==
              CONFIG_SOLAR_APPLICATION_DEFAULT_SERVICE_PRIORITY_LEVEL);

#if CONFIG_NUM_METAIRQ_PRIORITIES > 0
static_assert(kernel::Priority::meta_irq<0>().is_meta_irq());
#endif

int main()
{
    return 0;
}
