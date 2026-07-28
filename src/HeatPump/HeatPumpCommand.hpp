#pragma once

#include <cmath>
#include <cstdint>

#include <PoolWireProtocol.hpp>

#include "HeatPump/HeatPumpData.hpp"

namespace heatpump
{
    enum class HeatPumpCommandType : uint8_t
    {
        Power = 0,
        SetTemperature = 1,
        SetMode = 2,
        RequestStatus = 3,
        SafetyStop = 4
    };

    struct HeatPumpCommand
    {
        HeatPumpCommandType type = HeatPumpCommandType::RequestStatus;
        bool powerOn = false;
        float targetTemperature = NAN;
        HeatPumpMode mode = HeatPumpMode::Unknown;
        uint32_t commandId = 0U;
        uint32_t sequenceNumber = 0U;
        uint32_t localExpiresAtMs = 0U;
    };

    struct HeatPumpCommandResult
    {
        uint32_t commandId = 0U;
        HeatPumpCommandType type = HeatPumpCommandType::RequestStatus;
        poolwire::CommandAckStage stage = poolwire::CommandAckStage::Failed;
        poolwire::AckResult result = poolwire::AckResult::ApplicationFailed;
        uint16_t errorCode = 0U;
    };
}
