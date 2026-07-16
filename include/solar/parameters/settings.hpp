#pragma once

#include <array>
#include <cerrno>
#include <cstddef>
#include <span>

#include "solar/core/fixed_string.hpp"
#include "solar/core/status.hpp"
#include "solar/parameters/persistence.hpp"

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_PARAMETERS_ZEPHYR_SETTINGS)
#include <zephyr/settings/settings.h>
#endif

namespace solar::parameters::settings
{

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_PARAMETERS_ZEPHYR_SETTINGS)

template <FixedString Namespace> struct Store
{
    static_assert(!Namespace.empty(),
                  "SOLAR_DIAGNOSTIC_PARAMETER_SETTINGS_NAMESPACE: settings namespace cannot be "
                  "empty");

    [[nodiscard]] static Result<void> initialize() noexcept
    {
#if defined(CONFIG_SOLAR_PARAMETERS_SETTINGS_INITIALIZE)
        const auto result = settings_subsys_init();
        return result == 0 ? Result<void>{} : Result<void>{fail(status_from_errno(result))};
#else
        return {};
#endif
    }

    [[nodiscard]] static Result<std::size_t> load(persistence::Key key,
                                                  std::span<std::byte> output) noexcept
    {
        const auto name = key_name(key);
        const auto result = settings_load_one(name.data(), output.data(), output.size());
        if (result < 0) {
            return fail(status_from_errno(static_cast<int>(result)));
        }
        if (result == 0) {
            return fail(Status::NotFound);
        }
        return static_cast<std::size_t>(result);
    }

    [[nodiscard]] static Result<void> save(persistence::Key key,
                                           std::span<const std::byte> input) noexcept
    {
        const auto name = key_name(key);
        const auto result = settings_save_one(name.data(), input.data(), input.size());
        return result == 0 ? Result<void>{} : Result<void>{fail(status_from_errno(result))};
    }

    [[nodiscard]] static Result<void> erase(persistence::Key key) noexcept
    {
        const auto name = key_name(key);
        const auto result = settings_delete(name.data());
        return result == 0 || result == -ENOENT ? Result<void>{}
                                                : Result<void>{fail(status_from_errno(result))};
    }

  private:
    static constexpr std::size_t path_size = Namespace.size() + 20;

    [[nodiscard]] static constexpr std::array<char, path_size>
    key_name(persistence::Key key) noexcept
    {
        std::array<char, path_size> name{};
        std::size_t offset{};
        for (const auto character : Namespace.view()) {
            name[offset++] = character;
        }
        name[offset++] = '/';
        name[offset++] = key.kind == persistence::RecordKind::Group ? 'g' : 'p';
        name[offset++] = '/';
        constexpr char hexadecimal[] = "0123456789abcdef";
        for (int shift = 60; shift >= 0; shift -= 4) {
            name[offset++] = hexadecimal[(key.stable_id >> shift) & 0x0fU];
        }
        name[offset] = '\0';
        return name;
    }
};

using ZephyrStore = Store<CONFIG_SOLAR_PARAMETERS_SETTINGS_NAMESPACE>;

#endif

} // namespace solar::parameters::settings
