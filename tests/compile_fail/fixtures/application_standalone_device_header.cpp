// Regression guard: a self-logging, non-template device header must remain
// compilable as its own standalone translation unit -- i.e. with Application
// declared but never completed anywhere in this TU, exactly the situation a
// language server puts a single opened header in. This is precisely the
// false positive `Specification<Application, bool>` (see application.hpp)
// exists to prevent.
#define CONFIG_SOLAR_LOG 1
#define CONFIG_SOLAR_LOG_MAX_RECORD_BYTES 64
#define CONFIG_SOLAR_LOG_MAX_STRING_BYTES 32
#define CONFIG_SOLAR_LOG_MAX_HEXDUMP_BYTES 32

#include <solar/application.hpp>

namespace fixture
{

// Forward-declared only, exactly as the real generated headers forward
// declare the application type ahead of any device header that logs
// against it. Deliberately never completed in this translation unit.
struct Application;

struct Device
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.device"};

    static void init() noexcept
    {
        solar::log::For<Application>::info<Device>("initialized");
    }
};

} // namespace fixture

int main()
{
    fixture::Device::init();
}
