#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <atomic>
#include <cstdint>

#include <PoolWireProtocol.hpp>
#include "HeatPump/HeatPumpState.hpp"
#include "HeatPump/HeatPumpCommand.hpp"
#include "HeatPump/BridgeWatchdog.hpp"

namespace communication
{
    class EspNowBridge
    {
    public:
        EspNowBridge(heatpump::HeatPumpState& state, QueueHandle_t commandQueue, QueueHandle_t commandResultQueue);

        bool begin();

        void loopOnce();

    private:
        bool injectCommand(const heatpump::HeatPumpCommand& command);

        static void onRecvThunk(const esp_now_recv_info_t* info, const uint8_t* data, int len);

        static void onSentThunk(const wifi_tx_info_t* info, esp_now_send_status_t status);

        void onRecv(const uint8_t* mac, const uint8_t* data, int len);

        void onSent(const uint8_t* mac, esp_now_send_status_t status);

        bool initRadio();

        bool addPeer(const uint8_t mac[6]);

        bool addBroadcastPeer();

        void rememberPeer(const uint8_t mac[6]);
        bool snapshotPeer(uint8_t out[6]) const;
        void requestSafetyStop();

        void sendPairing(const uint8_t* mac, poolwire::PairingAction action);

        void sendCommandAck(const uint8_t* mac, uint32_t commandId, poolwire::CommandAckStage stage, poolwire::AckResult result, uint16_t errorCode = 0U);

        void sendStatus();

        [[nodiscard]] poolwire::HeatPumpTelemetry makeStatusPayload() const;

        [[nodiscard]] poolwire::Envelope nextEnvelope(poolwire::NodeId destination, uint32_t commandId = 0U);

        static bool isBroadcastMac(const uint8_t mac[6]);

        static bool isZeroMac(const uint8_t mac[6]);

        static void macToString(const uint8_t mac[6], char out[18]);

        static bool convertCommandPayload(const poolwire::HeatPumpCommand& payload, heatpump::HeatPumpCommand& outCommand);

        heatpump::HeatPumpState& state_;
        QueueHandle_t commandQueue_;
        QueueHandle_t commandResultQueue_;

        uint8_t peerMac_[6]{};
        bool peerValid_{false};
        mutable portMUX_TYPE stateMux_ = portMUX_INITIALIZER_UNLOCKED;
        portMUX_TYPE commandLedgerMux_ = portMUX_INITIALIZER_UNLOCKED;

        uint32_t lastStatusSendMs_{0};
        uint32_t lastFailureLogMs_{0};
        std::atomic<uint32_t> lastRxMs_{0U};
        heatpump::BridgeWatchdog bridgeWatchdog_;

        std::atomic<uint32_t> sendFailures_{0U};
        std::atomic<uint32_t> receiveDecodeFailures_{0U};
        std::atomic<uint32_t> commandQueueFailures_{0U};
        std::atomic<bool> peerChangePending_{false};

        std::atomic<uint32_t> nextSequence_{1U};
        poolwire::SequenceTracker receivedSequences_;
        poolwire::CommandLedger commandLedger_;

        static EspNowBridge* instance_;
    };
}
