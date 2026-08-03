#include <solar/parameters/store.hpp>

struct Parameter
{
    using Value = int;
    static constexpr const char* name = "value";
    static constexpr int default_value = 0;
};

int main()
{
    solar::parameters::Store<solar::parameters::Schema<Parameter>> store;
    (void)store.initialize();
    (void)store.set_many(solar::parameters::ValueAssignment<Parameter>{1},
                         solar::parameters::ValueAssignment<Parameter>{2});
}
