#include <solar/parameters/store.hpp>

struct Present
{
    using Value = int;
    static constexpr const char* name = "present";
    static constexpr int default_value = 0;
};

struct Missing
{
    using Value = int;
    static constexpr const char* name = "missing";
    static constexpr int default_value = 0;
};

int main()
{
    solar::parameters::Store<solar::parameters::Schema<Present>> store;
    (void)store.initialize();
    (void)store.get<Missing>();
}
