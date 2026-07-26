#include "Communication/EspNowBridge.hpp"

#include "Config/AppConfig.hpp"
#include "Logger/Logger.hpp"

#include <cstring>
#include <cstdio>

namespace communication
{
    namespace
    {
        constexpr uint8_t kBroadcastMac[6] =
        {
            0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF
        };
    }

    EspNowBridge* EspNowBridge::instance_ = nullptr;

    EspNowBridge::EspNowBridge(heatpump::HeatPumpState& state,QueueHandle_t commandQueue)
        : state_(state), commandQueue_(commandQueue)
    {
        instance_ = this;
    }

    void EspNowBridge::begin()
    {
        if (!initRadio())
        {
            logger::Logger::log(
                logger::Type::Comms,
                "ESP-NOW init failed");

            return;
        }

        addBroadcastPeer();

        uint8_t myMac[6]{};
        esp_wifi_get_mac(WIFI_IF_STA, myMac);

        char macText[18]{};
        macToString(myMac, macText);

        logger::Logger::log(
            logger::Type::Comms,
            "ESP-NOW ready mac=%s channel=%u",
            macText,
            static_cast<unsigned>(config::espnow::kWifiChannel));
    }

    bool EspNowBridge::initRadio()
    {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);

        esp_wifi_set_ps(WIFI_PS_NONE);

        esp_err_t result = esp_wifi_set_channel(config::espnow::kWifiChannel, WIFI_SECOND_CHAN_NONE);

        if (result != ESP_OK)
        {
            logger::Logger::log(logger::Type::Comms, "esp_wifi_set_channel failed err=%d", static_cast<int>(result));
        }

        result = esp_now_init();

        if (result != ESP_OK && result != ESP_ERR_ESPNOW_EXIST)
        {
            logger::Logger::log(logger::Type::Comms, "esp_now_init failed err=%d", static_cast<int>(result));
            return false;
        }

        esp_now_register_recv_cb(&EspNowBridge::onRecvThunk);

        esp_now_register_send_cb(&EspNowBridge::onSentThunk);

        return true;
    }

    bool EspNowBridge::addBroadcastPeer()
    {
        return addPeer(kBroadcastMac);
    }

    bool EspNowBridge::addPeer(const uint8_t mac[6])
    {
        if (mac == nullptr)
        {
            return false;
        }

        esp_now_peer_info_t peer{};
        std::memcpy(peer.peer_addr, mac, 6);

        peer.channel = 0;
        peer.encrypt = false;

        const esp_err_t result =
            esp_now_add_peer(&peer);

        if (result == ESP_OK || result == ESP_ERR_ESPNOW_EXIST)
        {
            return true;
        }

        char macText[18]{};
        macToString(mac, macText);

        logger::Logger::log(logger::Type::Comms, "esp_now_add_peer failed mac=%s err=%d", macText, static_cast<int>(result));

        return false;
    }

    void EspNowBridge::rememberPeer(const uint8_t mac[6])
    {
        if (mac == nullptr || isBroadcastMac(mac) || isZeroMac(mac))
        {
            return;
        }

        const bool changed = !peerValid_ || std::memcmp(peerMac_, mac, 6) != 0;

        std::memcpy(peerMac_, mac, 6);
        peerValid_ = true;

        addPeer(peerMac_);

        if (changed)
        {
            char macText[18]{};
            macToString(peerMac_, macText);

            logger::Logger::log(logger::Type::Comms, "ESP-NOW peer set to %s", macText);
        }
    }

    void EspNowBridge::loopOnce()
    {
        const uint32_t now = millis();

        if ((now - lastStatusSendMs_) >= config::espnow::kStatusSendPeriodMs)
        {
            lastStatusSendMs_ = now;
            sendStatus();
        }

        if ((now - lastStatusLogMs_) >= 5000)
        {
            lastStatusLogMs_ = now;

            const heatpump::HeatPumpData snapshot =
                state_.snapshot();

            logger::Logger::log(
                logger::Type::Comms,
                "status: water=%.1f target=%.1f current=%.2fA "
                "power=%u valid=%u comp=%u err=%u code=%u clip=%u "
                "peer=%u lastRxAge=%lu lastTxAge=%lu sendErr=%d sendCb=%u",
                snapshot.waterTemperature,
                snapshot.targetTemperature,
                snapshot.currentRMS,
                snapshot.powerOn ? 1U : 0U,
                snapshot.powerStateValid ? 1U : 0U,
                snapshot.compressorRunning ? 1U : 0U,
                snapshot.errorActive ? 1U : 0U,
                static_cast<unsigned>(snapshot.errorCode),
                snapshot.currentClipping ? 1U : 0U,
                peerValid_ ? 1U : 0U,
                static_cast<unsigned long>(lastRxMs_ == 0 ? 0 : now - lastRxMs_),
                static_cast<unsigned long>(lastTxMs_ == 0 ? 0 : now - lastTxMs_),
                static_cast<int>(lastSendResult_),
                static_cast<unsigned>(lastSendStatus_));
        }
    }

    HeatPumpDataPacket EspNowBridge::makeStatusPacket() const
    {
        const heatpump::HeatPumpData snapshot = state_.snapshot();

        HeatPumpDataPacket packet{};

        packet.magic = DISC_MAGIC;
        packet.kind = HEATPUMP_STATUS;

        packet.waterTemperature = snapshot.waterTemperature;

        packet.targetTemperature = snapshot.targetTemperature;

        packet.currentRMS = snapshot.currentRMS;

        packet.powerOn = snapshot.powerOn ? 1U : 0U;

        packet.powerStateValid = snapshot.powerStateValid ? 1U : 0U;

        packet.compressorRunning = snapshot.compressorRunning ? 1U : 0U;

        packet.errorActive = snapshot.errorActive ? 1U : 0U;

        packet.errorCode = snapshot.errorCode;

        packet.mode = static_cast<uint8_t>(snapshot.mode);

        packet.currentClipping = snapshot.currentClipping ? 1U : 0U;

        for (size_t i = 0; i < WF_SAMPLES; ++i)
        {
            packet.waveform[i] = snapshot.waveform[i];
        }

        return packet;
    }

    void EspNowBridge::sendStatus()
    {
        if (!config::espnow::kEnableStatusSend)
        {
            return;
        }

        if (!peerValid_ && !config::espnow::kBroadcastStatusWhenNoPeer)
        {
            return;
        }

        const HeatPumpDataPacket packet = makeStatusPacket();

        const uint8_t* target = peerValid_ ? peerMac_ : kBroadcastMac;

        lastSendResult_ = esp_now_send(target, reinterpret_cast<const uint8_t*>(&packet), sizeof(packet));

        lastTxMs_ = millis();

        if (lastSendResult_ != ESP_OK)
        {
            logger::Logger::log(logger::Type::Comms, "ESP-NOW status send failed err=%d", static_cast<int>(lastSendResult_));
        }
    }

    bool EspNowBridge::injectCommand(const heatpump::HeatPumpCommand& command)
    {
        if (commandQueue_ == nullptr)
        {
            return false;
        }

        return xQueueSend(commandQueue_, &command, pdMS_TO_TICKS(20)) == pdTRUE;
    }

    void EspNowBridge::onRecvThunk(const uint8_t* mac, const uint8_t* data, const int len)
    {
        if (instance_ != nullptr)
        {
            instance_->onRecv(mac, data, len);
        }
    }

    void EspNowBridge::onSentThunk(const uint8_t* mac, const esp_now_send_status_t status)
    {
        if (instance_ != nullptr)
        {
            instance_->onSent(mac, status);
        }
    }

    void EspNowBridge::onSent(const uint8_t* mac, const esp_now_send_status_t status)
    {
        (void)mac;

        lastSendStatus_ = status;

        if (status != ESP_NOW_SEND_SUCCESS)
        {
            logger::Logger::log(logger::Type::Comms, "ESP-NOW send callback: FAIL");
        }
    }

    void EspNowBridge::onRecv(const uint8_t* mac, const uint8_t* data, const int len)
    {
        if (mac == nullptr || data == nullptr || len <= 0)
        {
            return;
        }

        lastRxMs_ = millis();

        rememberPeer(mac);

        if (len >= static_cast<int>(sizeof(DiscoverPing)))
        {
            const auto* header = reinterpret_cast<const DiscoverPing*>(data);

            if (header->magic == DISC_MAGIC && header->kind == DISC_PING)
            {
                logger::Logger::log(logger::Type::Comms, "ESP-NOW discovery ping received");
                sendDiscoverAck(mac);
                return;
            }

            if (header->magic == DISC_MAGIC && header->kind == DISC_ACK && len >= static_cast<int>(sizeof(DiscoverAck)))
            {
                logger::Logger::log(logger::Type::Comms, "ESP-NOW discovery ack received");
                return;
            }
        }

        if (len == static_cast<int>(sizeof(HeatPumpCommandPacket)))
        {
            HeatPumpCommandPacket packet{};

            std::memcpy(&packet,data,sizeof(packet));

            if (packet.magic != DISC_MAGIC || packet.kind != HEATPUMP_COMMAND)
            {
                return;
            }

            heatpump::HeatPumpCommand command{};

            if (!convertCommandPacket(packet, command))
            {
                logger::Logger::log(logger::Type::Comms, "ESP-NOW heatpump command rejected");
                return;
            }

            const bool queued = injectCommand(command);

            logger::Logger::log(
                logger::Type::Comms,
                "ESP-NOW heatpump command received type=%u queued=%u",
                static_cast<unsigned>(packet.commandType),
                queued ? 1U : 0U);

            return;
        }

        logger::Logger::log(logger::Type::Comms, "ESP-NOW unknown packet len=%d", len);
    }

    void EspNowBridge::sendDiscoverAck(const uint8_t* mac)
    {
        if (mac == nullptr)
        {
            return;
        }

        uint8_t myMac[6]{};
        esp_wifi_get_mac(WIFI_IF_STA, myMac);

        DiscoverAck ack{};
        ack.magic = DISC_MAGIC;
        ack.kind = DISC_ACK;

        std::memcpy(ack.mac, myMac, 6);

        addPeer(mac);

        const esp_err_t result = esp_now_send(mac, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));

        if (result != ESP_OK)
        {
            logger::Logger::log(logger::Type::Comms, "DISC_ACK send failed err=%d", static_cast<int>(result));
        }
    }

    bool EspNowBridge::convertCommandPacket(const HeatPumpCommandPacket& packet, heatpump::HeatPumpCommand& outCommand)
    {
        using ControllerCommandType = heatpump::HeatPumpCommandType;

        switch (static_cast<HeatPumpCommandType>(packet.commandType))
        {
            case HeatPumpCommandType::Power:
                outCommand.type = ControllerCommandType::Power;

                outCommand.powerOn = packet.powerOn != 0;

                return true;

            case HeatPumpCommandType::SetTemperature:
                outCommand.type = ControllerCommandType::SetTemperature;

                outCommand.targetTemperature = packet.targetTemperature;

                return true;

            case HeatPumpCommandType::SetMode:
                outCommand.type = ControllerCommandType::SetMode;

                switch (static_cast<HeatPumpMode>( packet.mode))
                {
                    case HeatPumpMode::Heat:
                        outCommand.mode = heatpump::HeatPumpMode::Heat;
                        return true;

                    case HeatPumpMode::Cool:
                        outCommand.mode = heatpump::HeatPumpMode::Cool;
                        return true;

                    case HeatPumpMode::Auto:
                        outCommand.mode = heatpump::HeatPumpMode::Auto;
                        return true;

                    default:
                        return false;
                }

            case HeatPumpCommandType::RequestStatus:
                outCommand.type = ControllerCommandType::RequestStatus;

                return true;

            default:
                return false;
        }
    }

    bool EspNowBridge::isBroadcastMac(const uint8_t mac[6])
    {
        return std::memcmp(mac, kBroadcastMac, 6) == 0;
    }

    bool EspNowBridge::isZeroMac(const uint8_t mac[6])
    {
        static constexpr uint8_t zero[6] =
        {
            0, 0, 0,
            0, 0, 0
        };

        return std::memcmp(mac, zero, 6) == 0;
    }

    void EspNowBridge::macToString(const uint8_t mac[6], char out[18])
    {
        std::snprintf
        (
            out,
            18,
            "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0],
            mac[1],
            mac[2],
            mac[3],
            mac[4],
            mac[5]
        );
    }
}