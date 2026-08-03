#include <solar/application.hpp>

struct Contract
{
    using Parameters = solar::TypeList<>;
    using Data = solar::TypeList<>;
    using Actions = solar::TypeList<>;
    using OutputStreams = solar::TypeList<>;
    using InputStreams = solar::TypeList<>;
    using Events = solar::TypeList<>;
    using Metrics = solar::TypeList<>;
};
struct Service
{
    using Endpoints = solar::Endpoints<>;
};
struct Application
{
    using Contract = ::Contract;
    using Services = solar::Services<solar::Run<Service, solar::Stack<1024>, solar::Stack<2048>>>;
};
using System = solar::System<Application>;
static_assert(sizeof(System) != 0);
