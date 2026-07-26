#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cstdint>

#include "Communication/Protocol.hpp"
#include "HeatPump/HeatPumpState.hpp"
#include "HeatPump/HeatPumpController.hpp"

namespace communication
{
    class EspNowBridge
    {
    public:
        EspNowBridge( heatpump::HeatPumpState& state, QueueHandle_t commandQueue);

        void begin();

        void loopOnce();

        bool injectCommand(const heatpump::HeatPumpCommand& command);

    private:
        static void onRecvThunk(const uint8_t* mac, const uint8_t* data, int len);

        static void onSentThunk(const uint8_t* mac, esp_now_send_status_t status);

        void onRecv(const uint8_t* mac, const uint8_t* data, int len);

        void onSent(const uint8_t* mac, esp_now_send_status_t status);

        bool initRadio();

        bool addPeer(const uint8_t mac[6]);

        bool addBroadcastPeer();

        void rememberPeer(const uint8_t mac[6]);

        void sendDiscoverAck(const uint8_t* mac);

        void sendStatus();

        [[nodiscard]] HeatPumpDataPacket makeStatusPacket() const;

        static bool isBroadcastMac(const uint8_t mac[6]);

        static bool isZeroMac(const uint8_t mac[6]);

        static void macToString(const uint8_t mac[6], char out[18]);

        static bool convertCommandPacket(const HeatPumpCommandPacket& packet, heatpump::HeatPumpCommand& outCommand);

        heatpump::HeatPumpState& state_;
        QueueHandle_t commandQueue_;

        uint8_t peerMac_[6]{};
        bool peerValid_{false};

        uint32_t lastStatusSendMs_{0};
        uint32_t lastStatusLogMs_{0};
        uint32_t lastRxMs_{0};
        uint32_t lastTxMs_{0};

        esp_err_t lastSendResult_{ESP_FAIL};

        esp_now_send_status_t lastSendStatus_{ESP_NOW_SEND_FAIL};

        static EspNowBridge* instance_;
    };
}