#include "HeatPump/BridgeWatchdog.hpp"
#include "HeatPump/HeatPumpData.hpp"
#include "HeatPump/NetBusArbiter.hpp"
#include "HeatPump/NetConfiguration.hpp"
#include "HeatPump/NetProtocol.hpp"
#include "HeatPump/PowerCycleGuard.hpp"
#include "Config/AppConfig.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>

namespace
{
    int failures = 0;

    void expect(const bool condition, const std::string_view message)
    {
        if (condition) return;
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }

    std::array<uint8_t, heatpump::NetConfiguration::kFrameSize> configFrame()
    {
        return {0x81U, 0xB1U, 0x26U, 0x72U, 0x76U, 0x74U,
                0x3DU, 0x3DU, 0x3DU, 0x3DU, 0x3CU, 0xE4U};
    }

    heatpump::NetRawFrame longFrame(const uint8_t type)
    {
        heatpump::NetRawFrame frame{};
        frame.timestampMs = 1000U;
        frame.bitCount = 96U;
        frame.byteCount = 12U;
        frame.bytes[0] = type;
        frame.bytes[1] = 0xB1U;
        for (size_t index = 2U; index < 11U; ++index)
        {
            frame.bytes[index] = static_cast<uint8_t>(0x20U + index);
        }
        frame.bytes[11] = heatpump::NetConfiguration::checksum(frame.bytes);
        return frame;
    }

    void testChecksumsAndLengths()
    {
        heatpump::NetProtocol protocol;
        auto frame = longFrame(0xD1U);
        expect(protocol.checksumLooksValid(frame), "valid 12-byte checksum accepted");
        frame.bytes[5] ^= 0x01U;
        expect(!protocol.checksumLooksValid(frame), "corrupt checksum rejected");
        frame = longFrame(0xD1U);
        frame.bitCount = 95U;
        expect(!protocol.checksumLooksValid(frame), "partial frame length rejected");

        heatpump::NetRawFrame shortFrame{};
        shortFrame.bitCount = 72U;
        shortFrame.byteCount = 9U;
        shortFrame.bytes[2] = 1U;
        shortFrame.bytes[3] = 2U;
        shortFrame.bytes[4] = 3U;
        shortFrame.bytes[5] = 4U;
        shortFrame.bytes[6] = 5U;
        shortFrame.bytes[7] = 6U;
        shortFrame.bytes[8] = 21U;
        expect(protocol.checksumLooksValid(shortFrame), "valid 9-byte checksum accepted");
    }

    void testFrameKindsAndPartialUpdates()
    {
        heatpump::NetProtocol protocol;
        heatpump::HeatPumpData decoded{};

        auto d1 = longFrame(0xD1U);
        d1.bytes[9] = 0x74U;
        d1.bytes[11] = heatpump::NetConfiguration::checksum(d1.bytes);
        expect(protocol.decode(d1, decoded), "D1 frame decoded");
        expect(heatpump::hasField(decoded, heatpump::WaterTemperatureField),
               "D1 changes water temperature only");
        expect(!heatpump::hasField(decoded, heatpump::PowerStateField),
               "D1 does not invent power state");

        heatpump::HeatPumpData current{};
        current.targetTemperature = 29.0F;
        current.mode = heatpump::HeatPumpMode::Heat;
        current.powerOn = true;
        current.powerStateValid = true;
        current.errorActive = true;
        current.errorCode = 7U;
        heatpump::mergeDecodedData(current, decoded, 1000U);
        expect(std::fabs(current.targetTemperature - 29.0F) < 0.01F,
               "partial update preserves target");
        expect(current.mode == heatpump::HeatPumpMode::Heat,
               "partial update preserves mode");
        expect(current.powerOn && current.powerStateValid,
               "partial update preserves power");
        expect(current.errorActive && current.errorCode == 7U,
               "partial update preserves absent error fields");

        heatpump::HeatPumpData d2Decoded{};
        expect(protocol.decode(longFrame(0xD2U), d2Decoded), "D2 frame accepted");
        expect(d2Decoded.validFields == 0U, "D2 leaves unsupported fields untouched");

        auto config = configFrame();
        heatpump::NetRawFrame configRaw{};
        configRaw.timestampMs = 1200U;
        configRaw.bitCount = 96U;
        configRaw.byteCount = 12U;
        std::memcpy(configRaw.bytes, config.data(), config.size());
        heatpump::HeatPumpData configDecoded{};
        expect(protocol.decode(configRaw, configDecoded), "0x81 configuration decoded");
        expect(configDecoded.configurationValid, "0x81 captures full configuration");
    }

    void expectPreserved(const std::array<uint8_t, 12>& before,
                         const uint8_t* after,
                         const size_t changedIndex,
                         const std::string_view message)
    {
        for (size_t index = 0U; index < before.size() - 1U; ++index)
        {
            if (index == changedIndex) continue;
            expect(before[index] == after[index], message);
        }
    }

    void testConfigurationMutations()
    {
        const auto before = configFrame();
        expect(heatpump::NetConfiguration::isValid(before.data(), before.size()),
               "captured configuration is valid");
        uint8_t output[12]{};

        heatpump::HeatPumpCommand power{};
        power.type = heatpump::HeatPumpCommandType::Power;
        power.powerOn = true;
        expect(heatpump::NetConfiguration::apply(before.data(), power, output),
               "power mutation built");
        expect((output[2] & 0x01U) != 0U, "power bit changed");
        const std::array<uint8_t, 12> expectedPowerOn =
            {0x81U, 0xB1U, 0x27U, 0x72U, 0x76U, 0x74U,
             0x3DU, 0x3DU, 0x3DU, 0x3DU, 0x3CU, 0xE5U};
        expect(std::memcmp(output, expectedPowerOn.data(), expectedPowerOn.size()) == 0,
               "power command has expected byte sequence");
        expectPreserved(before, output, 2U, "power preserves unrelated settings");
        expect(heatpump::NetConfiguration::isValid(output, sizeof(output)),
               "power mutation checksum updated");

        heatpump::HeatPumpCommand setpoint{};
        setpoint.type = heatpump::HeatPumpCommandType::SetTemperature;
        setpoint.targetTemperature = 30.0F;
        expect(heatpump::NetConfiguration::apply(before.data(), setpoint, output),
               "setpoint mutation built");
        expect(output[5] != before[5], "auto setpoint field changed");
        const std::array<uint8_t, 12> expectedSetpoint =
            {0x81U, 0xB1U, 0x26U, 0x72U, 0x76U, 0x78U,
             0x3DU, 0x3DU, 0x3DU, 0x3DU, 0x3CU, 0xE8U};
        expect(std::memcmp(output, expectedSetpoint.data(), expectedSetpoint.size()) == 0,
               "setpoint command has expected byte sequence");
        expectPreserved(before, output, 5U, "setpoint preserves unrelated settings");

        heatpump::HeatPumpCommand mode{};
        mode.type = heatpump::HeatPumpCommandType::SetMode;
        mode.mode = heatpump::HeatPumpMode::Heat;
        expect(heatpump::NetConfiguration::apply(before.data(), mode, output),
               "mode mutation built");
        expect((output[2] & 0x30U) == 0x10U, "mode bits changed to heat");
        const std::array<uint8_t, 12> expectedHeatMode =
            {0x81U, 0xB1U, 0x16U, 0x72U, 0x76U, 0x74U,
             0x3DU, 0x3DU, 0x3DU, 0x3DU, 0x3CU, 0xD4U};
        expect(std::memcmp(output, expectedHeatMode.data(), expectedHeatMode.size()) == 0,
               "mode command has expected byte sequence");
        expectPreserved(before, output, 2U, "mode preserves unrelated settings");

        uint8_t unknown[12]{};
        expect(!heatpump::NetConfiguration::apply(unknown, power, output),
               "normal command rejected without valid source configuration");
    }

    void testCommandTransmissionProfile()
    {
        expect(config::netbus::kTxHighZeroUs == 1000U,
               "command zero uses short HIGH pulse");
        expect(config::netbus::kTxHighOneUs == 3000U,
               "command one uses long HIGH pulse");
        expect(config::netbus::kTxCommandRepeatCount == 8U,
               "command frame is repeated eight times");
        expect(config::netbus::kTxInterFrameLowUs == 1000U &&
               config::netbus::kTxInterFrameHighUs == 100000U,
               "command repetitions use captured NET-bus spacing");
    }

    void testArbitrationAndProtectionTimes()
    {
        heatpump::NetBusArbiter arbiter(1000U);
        expect(arbiter.beginReceive(), "RX starts while idle");
        expect(!arbiter.beginTransmit(0U), "TX cannot start during RX");
        arbiter.endReceive();
        expect(arbiter.beginTransmit(0U), "TX starts after RX releases owner");
        expect(!arbiter.beginReceive(), "RX cannot start during TX");
        arbiter.endTransmit(10U);
        expect(!arbiter.beginTransmit(1009U), "minimum NET command interval enforced");
        expect(arbiter.beginTransmit(1010U), "TX allowed after command interval");
        arbiter.endTransmit(1010U);

        heatpump::PowerCycleGuard guard(180000U, 180000U);
        guard.observe(false, 0U);
        expect(!guard.allows(true, 179999U), "minimum off-time enforced");
        expect(guard.allows(true, 180000U), "start allowed after off-time");
        guard.observe(true, 180000U);
        expect(!guard.allows(false, 359999U), "minimum run-time enforced");
        expect(guard.allows(false, 360000U), "stop allowed after run-time");
    }

    void testNetOutageAndReconnect()
    {
        heatpump::BridgeWatchdog watchdog(10000U);
        watchdog.start(0U);
        expect(!watchdog.update(9999U, 0U).requestSafetyStop,
               "startup grace does not stop early");
        expect(watchdog.update(10000U, 0U).requestSafetyStop,
               "bridge outage requests safe stop");
        const auto reconnect = watchdog.update(12000U, 12000U);
        expect(reconnect.reconnected && watchdog.online(),
               "fresh heartbeat marks reconnection without start command");
        expect(!reconnect.requestSafetyStop, "reconnect does not request actuator start");
        expect(watchdog.update(22001U, 12000U).requestSafetyStop,
               "renewed heartbeat loss requests safe stop again");
    }
}

int main()
{
    testChecksumsAndLengths();
    testFrameKindsAndPartialUpdates();
    testConfigurationMutations();
    testCommandTransmissionProfile();
    testArbitrationAndProtectionTimes();
    testNetOutageAndReconnect();
    if (failures == 0) std::cout << "HeatPumpLogicTests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
