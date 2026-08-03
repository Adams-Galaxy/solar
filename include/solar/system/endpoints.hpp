#pragma once

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

#include "solar/core/status.hpp"
#include "solar/core/type_list.hpp"

namespace solar
{

/** One service's complete, generated endpoint ownership table. */
template <typename... Bindings> struct Endpoints
{
    using Entries = TypeList<Bindings...>;
};

namespace endpoint
{

struct ReadTag
{};
struct HandleTag
{};
struct OutputTag
{};
struct InputTag
{};
struct EmitTag
{};
struct ObserveTag
{};
struct RecordTag
{};
struct UseTag
{};

/** Declare typed use of a standalone module schema entry such as a parameter. */
template <typename Module, typename Entry> struct Use
{
    using EndpointType = Entry;
    using ModuleType = Module;
    using Kind = UseTag;
};

/** Own a query-only Data endpoint. */
template <typename Endpoint, auto Reader> struct Read
{
    using EndpointType = Endpoint;
    using Kind = ReadTag;
    static constexpr auto reader = Reader;

    [[nodiscard]] static typename Endpoint::Value query()
    {
        return std::invoke(Reader);
    }
};

/** Own a query/update Data endpoint. */
template <typename Endpoint, auto Reader, auto Writer> struct ReadWrite : Read<Endpoint, Reader>
{
    static constexpr auto writer = Writer;

    [[nodiscard]] static Result<void> update(const typename Endpoint::Value& value)
    {
        return std::invoke(Writer, value);
    }
};

/** Own an Action endpoint. */
template <typename Endpoint, auto Handler> struct Handle
{
    using EndpointType = Endpoint;
    using Kind = HandleTag;
    static constexpr auto handler = Handler;

    [[nodiscard]] static typename Endpoint::Response call(const typename Endpoint::Request& request)
    {
        return std::invoke(Handler, request);
    }
};

/** Own an output Stream endpoint. */
template <typename Endpoint, auto Publisher> struct Output
{
    using EndpointType = Endpoint;
    using Kind = OutputTag;
    static constexpr auto publisher = Publisher;

    [[nodiscard]] static typename Endpoint::Value publish()
    {
        return std::invoke(Publisher);
    }
};

/** Own an input Stream. Open and close hooks are optional. */
template <typename Endpoint, auto Consumer, auto OpenHook = nullptr, auto CloseHook = nullptr>
struct Input
{
    using EndpointType = Endpoint;
    using Kind = InputTag;
    static constexpr auto consumer = Consumer;
    static constexpr auto open_hook = OpenHook;
    static constexpr auto close_hook = CloseHook;

    [[nodiscard]] static Result<void> consume(const typename Endpoint::Value& value)
    {
        return std::invoke(Consumer, value);
    }

    template <typename Context> [[nodiscard]] static Result<void> open(const Context& context)
    {
        if constexpr (std::is_same_v<decltype(OpenHook), std::nullptr_t>) {
            return {};
        } else {
            return std::invoke(OpenHook, context);
        }
    }

    template <typename Context> static void close(const Context& context)
    {
        if constexpr (!std::is_same_v<decltype(CloseHook), std::nullptr_t>) {
            std::invoke(CloseHook, context);
        }
    }
};

/** Own emission of an Event endpoint. */
template <typename Endpoint> struct Emit
{
    using EndpointType = Endpoint;
    using Kind = EmitTag;
};

/** Observe an Event endpoint. Multiple observers are valid. */
template <typename Endpoint, auto Observer> struct Observe
{
    using EndpointType = Endpoint;
    using Kind = ObserveTag;
    static constexpr auto observer = Observer;

    static void observe(const typename Endpoint::Value& value)
    {
        std::invoke(Observer, value);
    }
};

/** Own a Metric endpoint. */
template <typename Endpoint, auto Recorder> struct Record
{
    using EndpointType = Endpoint;
    using Kind = RecordTag;
    static constexpr auto recorder = Recorder;

    [[nodiscard]] static Result<void> record(const typename Endpoint::Value& value)
    {
        return std::invoke(Recorder, value);
    }
};

} // namespace endpoint
} // namespace solar
