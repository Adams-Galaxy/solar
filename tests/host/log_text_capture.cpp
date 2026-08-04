#include <array>
#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>

#include <solar/log/declaration.hpp>
#include <solar/log/static_logger.hpp>

// Exercises the runtime-text capture path added for the Zephyr backend
// bridge: a Source with no compile-time format string, rendered verbatim
// rather than through the native argument-encoded path. This is the same
// primitive the bridge uses to relay foreign Zephyr log lines, but it is
// host-testable on its own since it never touches Zephyr.

namespace fixture
{
struct Application
{};
struct BridgeSource
{
    static constexpr solar::log::SourceDescriptor descriptor{
        .name = "fixture.bridge",
        .description = "Stand-in for the Zephyr backend bridge Source",
    };
};
struct TextSink
{
    static solar::Result<void> consume(solar::log::RecordView record,
                                       std::string_view rendered) noexcept
    {
        last_origin = record.header.origin;
        last_level = record.header.level;
        last_size = rendered.copy(last.data(), last.size());
        ++writes;
        return {};
    }

    inline static std::array<char, 64> last{};
    inline static std::size_t last_size{};
    inline static std::size_t writes{};
    inline static solar::log::Origin last_origin{};
    inline static solar::log::Level last_level{};
};
} // namespace fixture

int main()
{
    using Logger =
        solar::log::StaticLogger<fixture::Application, solar::TypeList<fixture::BridgeSource>,
                                 solar::TypeList<solar::log::domain::Unclassified>,
                                 solar::TypeList<fixture::TextSink>, 2>;
    assert(Logger::initialize());
    assert(Logger::start());

    const auto receipt = Logger::capture_text<fixture::BridgeSource>(
        solar::log::Level::Warning, solar::log::Origin::Zephyr, "some_module: something happened");
    assert(receipt);
    assert(fixture::TextSink::writes == 1);
    assert(fixture::TextSink::last_origin == solar::log::Origin::Zephyr);
    assert(fixture::TextSink::last_level == solar::log::Level::Warning);
    const std::string_view rendered{fixture::TextSink::last.data(), fixture::TextSink::last_size};
    assert(rendered == "some_module: something happened");

    // Truncation: text longer than the configured payload is bounded, not
    // rejected, and the record is flagged rather than silently corrupted.
    const std::string long_text(fixture::TextSink::last.size() * 4, 'x');
    const auto truncated =
        Logger::capture_text<fixture::BridgeSource>(solar::log::Level::Info,
                                                     solar::log::Origin::Zephyr, long_text);
    assert(truncated);
    assert((truncated->flags & solar::log::flag(solar::log::RecordFlag::Truncated)) != 0);

    assert(Logger::stop());
    assert(Logger::deinitialize());
    return 0;
}
