#include "Logger/Logger.hpp"

namespace logger
{
    void Logger::begin(uint32_t) {}
    void Logger::log(Level, Type, const char*, ...) {}
    const char* Logger::levelTag(Level) { return "TEST"; }
    const char* Logger::tag(Type) { return "TEST"; }
}
