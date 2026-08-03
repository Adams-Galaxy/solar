#include <solar/system/composer.hpp>

struct Second;
struct First
{
    using Dependencies = solar::TypeList<Second>;
};
struct Second
{
    using Dependencies = solar::TypeList<First>;
};
struct Application;
using Invalid = solar::system::System<Application, solar::Compose<solar::Own<First, Second>>>;
static_assert(sizeof(Invalid) > 0);
