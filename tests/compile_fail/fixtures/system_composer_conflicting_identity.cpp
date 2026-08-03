#include <solar/system/composer.hpp>

struct Application;
struct First
{
    static constexpr const char* name = "shared";
};
struct Second
{
    static constexpr const char* name = "shared";
};
using Subject = solar::system::System<Application, solar::Compose<solar::Own<First, Second>>>;

int main()
{
    return Subject::boot() ? 0 : 1;
}
