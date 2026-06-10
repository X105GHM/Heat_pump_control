#pragma once
#include <Arduino.h>

namespace logger
{
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
        static void log(Type type, const char* fmt, ...);
        static const char* tag(Type type);
    };
}
