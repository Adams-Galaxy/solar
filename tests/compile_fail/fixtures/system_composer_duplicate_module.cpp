#include <solar/system/composer.hpp>

struct Application;
struct Module
{};
using Invalid = solar::system::System<Application, solar::Compose<solar::Own<Module, Module>>>;
static_assert(sizeof(Invalid) > 0);
