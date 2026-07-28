#include "Logger/Logger.hpp"

namespace logger
{
    void Logger::begin(uint32_t) {}
    void Logger::log(Type, const char*, ...) {}
    const char* Logger::tag(Type) { return "TEST"; }
}
