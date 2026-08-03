#include <solar/system/composer.hpp>

struct Missing;
struct Module
{
    using Dependencies = solar::TypeList<Missing>;
};
struct Application;
using Invalid = solar::system::System<Application, solar::Compose<solar::Own<Module>>>;
static_assert(sizeof(Invalid) > 0);
