#include <solar/log/system.hpp>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(solar_system);

namespace solar::log::system
{

void error(const char* text)
{
    LOG_ERR("%s", text);
}

void warn(const char* text)
{
    LOG_WRN("%s", text);
}

void info(const char* text)
{
    LOG_INF("%s", text);
}

void debug(const char* text)
{
    LOG_DBG("%s", text);
}

} // namespace solar::log::system
