#include "solar/system.hpp"

struct Board { using Name = solar::Name<"board">; };
struct Missing { using Name = solar::Name<"missing">; };
struct Component { using Name = solar::Name<"component">; };

#if SOLAR_GRAPH_CASE_MISSING_DEPENDENCY
struct Dependant
{
    using Name = solar::Name<"dependant">;
    using Dependencies = solar::Dependencies<Missing>;
};
using Invalid = solar::System<Board, solar::Peripherals<>, solar::Devices<Dependant>,
                              solar::Facilities<>, solar::Services<>>;
#elif SOLAR_GRAPH_CASE_DUPLICATE_COMPONENT
using Invalid = solar::System<Board, solar::Peripherals<Component>, solar::Devices<Component>,
                              solar::Facilities<>, solar::Services<>>;
#elif SOLAR_GRAPH_CASE_DUPLICATE_SERVICE
struct Service
{
    using Name = solar::Name<"service">;
    using Thread = solar::ServiceSpec<Name, 1024>;
    static void run(solar::StopToken) {}
};
using Invalid = solar::System<Board, solar::Peripherals<>, solar::Devices<>,
                              solar::Facilities<>, solar::Services<Service, Service>>;
#elif SOLAR_GRAPH_CASE_DEPENDENCY_CYCLE
struct Second;
struct First
{
    using Name = solar::Name<"first">;
    using Dependencies = solar::Dependencies<Second>;
};
struct Second
{
    using Name = solar::Name<"second">;
    using Dependencies = solar::Dependencies<First>;
};
using Invalid = solar::System<Board, solar::Peripherals<First>, solar::Devices<Second>,
                              solar::Facilities<>, solar::Services<>>;
#endif

static_assert(sizeof(Invalid) > 0);
int main() { return 0; }
