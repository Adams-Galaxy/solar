#include <type_traits>

#include <solar/application.hpp>
#include <solar/kernel/priority.hpp>

namespace kernel = solar::kernel;

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
