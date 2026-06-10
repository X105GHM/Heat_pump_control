#include "Logger/Logger.hpp"
#include <cstdarg>
#include <cstdio>

namespace logger
{
    void Logger::begin(const uint32_t baud)
    {
        Serial.begin(baud);
        delay(250);
        Serial.println();
        Serial.println(F("HeatPumpSmartESP32 boot"));
    }

    const char* Logger::tag(const Type type)
    {
        switch (type) {
            case Type::General:  return "GENERAL";
            case Type::NetBus:   return "NET";
            case Type::Protocol: return "PROTO";
            case Type::Current:  return "CURRENT";
            case Type::Control:  return "CONTROL";
            case Type::Comms:    return "COMMS";
            default:             return "UNKNOWN";
        }
    }

    void Logger::log(const Type type, const char* fmt, ...)
    {
        char buffer[192]{};
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        Serial.printf("[%10lu] [%s] %s\n", millis(), tag(type), buffer);
    }
}
