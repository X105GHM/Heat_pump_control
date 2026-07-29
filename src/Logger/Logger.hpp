#pragma once
#include <Arduino.h>

#ifndef HEAT_PUMP_LOG_LEVEL
#define HEAT_PUMP_LOG_LEVEL 2
#endif

namespace logger
{
    enum class Level : uint8_t
    {
        Error = 0,
        Warn = 1,
        Info = 2,
        Debug = 3,
        Verbose = 4
    };

    enum class Type : uint8_t
    {
        General,
        NetBus,
        Protocol,
        Current,
        Control,
        Comms
    };

    class Logger
    {
    public:
        static void begin(uint32_t baud);
        static void log(Level level, Type type, const char* fmt, ...);
        static const char* levelTag(Level level);
        static const char* tag(Type type);
    };
}
