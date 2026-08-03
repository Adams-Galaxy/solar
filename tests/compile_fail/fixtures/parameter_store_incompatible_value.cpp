#include <solar/parameters/store.hpp>

struct Gain
{
    using Value = float;
    static constexpr const char* name = "gain";
    static constexpr Value default_value = 1.0F;
};

int main()
{
    solar::parameters::Store<solar::parameters::Schema<Gain>> store;
    (void)store.initialize();
    (void)store.set<Gain>(1); // Exact typing intentionally rejects int-to-float coercion.
}
