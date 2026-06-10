#pragma once
#include <cstdint>
#include "Core/Types.hpp"
#include "HeatPump/HeatPumpController.hpp"
#include "HeatPump/HeatPumpState.hpp"

namespace communication
{
    static constexpr uint32_t HEATPUMP_MAGIC = 0x48504D50u; // 'HPMP'

    enum : uint8_t
    {
        DISC_PING = 1,
        DISC_ACK  = 2,
        DATA_STATUS = 10,
        DATA_COMMAND = 11
    };

    struct __attribute__((packed)) DiscoverPing
    {
        uint32_t magic = HEATPUMP_MAGIC;
        uint8_t kind = DISC_PING;
        uint8_t mac[6]{};
    };

    struct __attribute__((packed)) DiscoverAck
    {
        uint32_t magic = HEATPUMP_MAGIC;
        uint8_t kind = DISC_ACK;
        uint8_t mac[6]{};
    };

    struct HeatPumpDataPacket
    {
        uint32_t magic = HEATPUMP_MAGIC;
        uint8_t kind = DATA_STATUS;
        heatpump::HeatPumpData data{};
    };

    struct HeatPumpCommandPacket
    {
        uint32_t magic = HEATPUMP_MAGIC;
        uint8_t kind = DATA_COMMAND;
        heatpump::HeatPumpCommand command{};
    };
}
