#pragma once

#include <array>
#include "solar/remote/schema.hpp"

namespace solar::remote::generated
{
inline constexpr ::solar::remote::TypeDescriptor ManifestTypes[] = {
    {1330936816, "solar.Empty", 1, 0},
    {2712283464, "solar.HelloRequest", 1, 132},
    {855395094, "solar.HelloResponse", 1, 18},
    {246231336, "solar.ListRequest", 1, 4},
    {2970897027, "solar.RemoteSummary", 1, 10},
    {1473714712, "solar.MethodInfo", 1, 82},
    {3235887303, "solar.MethodListResponse", 1, 660},
    {2397281786, "solar.TopicInfo", 1, 78},
    {3407303909, "solar.TopicListResponse", 1, 628},
    {1145008616, "solar.ObservableInfo", 1, 82},
    {2408004759, "solar.ObservableListResponse", 1, 660},
    {3052413544, "solar.SubscribeRequest", 1, 7},
    {4144169654, "solar.SubscribeResponse", 1, 5},
    {4261092075, "solar.UnsubscribeRequest", 1, 2},
    {1839167045, "solar.TypeInfo", 1, 76},
    {2496778270, "solar.TypeListResponse", 1, 612},
    {1569356010, "solar.ComponentInfo", 1, 132},
    {179745493, "solar.ComponentListResponse", 1, 1076},
    {3976950404, "solar.BootReportResponse", 1, 40},
    {3211423560, "solar.ErrorResponse", 1, 78},
};
inline constexpr ::solar::remote::MethodDescriptor ManifestMethods[] = {
    {4265343945, "solar.hello", 2712283464, 855395094, 1},
    {901500017, "solar.remote.summary", 1330936816, 2970897027, 1},
    {4249693017, "solar.remote.methods.list", 246231336, 3235887303, 1},
    {806371283, "solar.remote.topics.list", 246231336, 3407303909, 1},
    {2027011001, "solar.remote.observables.list", 246231336, 2408004759, 1},
    {3089095056, "solar.remote.types.list", 246231336, 2496778270, 1},
    {1300657669, "solar.graph.components.list", 246231336, 179745493, 1},
    {991117241, "solar.boot.report", 1330936816, 3976950404, 1},
};
inline constexpr std::array<::solar::remote::TopicDescriptor, 0> ManifestTopics = {{
}};
inline constexpr std::array<::solar::remote::ObservableDescriptor, 0> ManifestObservables = {{
}};
} // namespace solar::remote::generated
