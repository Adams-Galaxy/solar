#include <solar/system/composer.hpp>

struct Module
{};
struct Adapter
{
    template <typename> static int connect()
    {
        return 0;
    }
};
struct Application;
using System =
    solar::system::System<Application,
                          solar::Compose<solar::Own<Module>, solar::Connect<Adapter, Module>>>;

int main()
{
    (void)System::boot();
}
