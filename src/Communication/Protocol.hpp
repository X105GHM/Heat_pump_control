#pragma once

#include <cstdint>
#include <cstddef>

#include "Core/Types.hpp"

namespace communication
{
    static constexpr uint32_t DISC_MAGIC = 0x504F4F4Cu; // "POOL"

    enum : uint8_t
    {
        DISC_PING = 1,
        DISC_ACK  = 2,
        HEATPUMP_STATUS  = 20,
        HEATPUMP_COMMAND = 21
    };

    struct __attribute__((packed)) DiscoverPing
    {
        uint32_t magic = DISC_MAGIC;
        uint8_t kind = DISC_PING;
        uint8_t mac[6]{};
    };

    struct __attribute__((packed)) DiscoverAck
    {
        uint32_t magic = DISC_MAGIC;
        uint8_t kind = DISC_ACK;
        uint8_t mac[6]{};
    };

    enum CommandType : uint8_t
    {
        Pump   = 0,
        Freeze = 1,
        Fan    = 2
    };

    struct ControlCommand
    {
        CommandType cmdType;
        bool turnOn;
    };

    struct PoolData
    {
        float tempIn;
        float tempOut;
        float currentRMS;
        bool pumpOn;
        bool freezeOn;
        bool fanOn;
        float waveform[WF_SAMPLES];
    };

    enum class HeatPumpCommandType : uint8_t
    {
        Power = 0,
        SetTemperature = 1,
        SetMode = 2,
        RequestStatus = 3
    };

    enum class HeatPumpMode : uint8_t
    {
        Heat = 0,
        Cool = 1,
        Auto = 2,
        Unknown = 255
    };

    struct __attribute__((packed)) HeatPumpCommandPacket
    {
        uint32_t magic = DISC_MAGIC;
        uint8_t kind = HEATPUMP_COMMAND;

        uint8_t commandType = static_cast<uint8_t>(HeatPumpCommandType::RequestStatus);

        uint8_t powerOn = 0;
        float targetTemperature = 0.0F;

        uint8_t mode = static_cast<uint8_t>(HeatPumpMode::Unknown);
    };

    struct __attribute__((packed)) HeatPumpDataPacket
    {
        uint32_t magic = DISC_MAGIC;
        uint8_t kind = HEATPUMP_STATUS;

        float waterTemperature = 0.0F;
        float targetTemperature = 0.0F;
        float currentRMS = 0.0F;

        uint8_t powerOn = 0;
        uint8_t powerStateValid = 0;
        uint8_t compressorRunning = 0;

        uint8_t errorActive = 0;
        uint8_t errorCode = 0;

        uint8_t mode = static_cast<uint8_t>(HeatPumpMode::Unknown);

        uint8_t currentClipping = 0;

        float waveform[WF_SAMPLES]{};
    };
}