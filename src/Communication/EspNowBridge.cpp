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

    EspNowBridge::EspNowBridge(heatpump::HeatPumpState& state,
                               QueueHandle_t commandQueue,
                               QueueHandle_t commandResultQueue)
        : state_(state),
          commandQueue_(commandQueue),
          commandResultQueue_(commandResultQueue),
          bridgeWatchdog_(config::control::kBridgeConnectionTimeoutMs)
    {
        instance_ = this;
    }

    poolwire::Envelope EspNowBridge::nextEnvelope(const poolwire::NodeId destination,
                                                  const uint32_t commandId)
    {
        poolwire::Envelope envelope{};
        envelope.source = poolwire::NodeId::HeatPump;
        envelope.destination = destination;
        envelope.sequenceNumber = nextSequence_.fetch_add(1U);
        envelope.commandId = commandId;
        return envelope;
    }

    void EspNowBridge::begin()
    {
        bridgeWatchdog_.start(millis());
        if (!initRadio())
        {
            logger::Logger::log(
                logger::Type::Comms,
                "ESP-NOW init failed");

            return;
        }

        addBroadcastPeer();

        poolwire::Discovery discovery{};
        poolwire::EncodedFrame discoveryFrame{};

        if (poolwire::encodeMessage(
                nextEnvelope(poolwire::NodeId::Broadcast),
                discovery,
                discoveryFrame))
        {
            esp_now_send(kBroadcastMac, discoveryFrame.data(), discoveryFrame.size);
        }

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

        bool changed = false;
        portENTER_CRITICAL(&stateMux_);
        changed = !peerValid_ || std::memcmp(peerMac_, mac, 6) != 0;
        std::memcpy(peerMac_, mac, 6);
        peerValid_ = true;
        portEXIT_CRITICAL(&stateMux_);

        addPeer(mac);

        if (changed)
        {
            char macText[18]{};
            macToString(mac, macText);

            logger::Logger::log(logger::Type::Comms, "ESP-NOW peer set to %s", macText);
        }
    }

    void EspNowBridge::loopOnce()
    {
        heatpump::HeatPumpCommandResult commandResult{};

        while (commandResultQueue_ != nullptr &&
               xQueueReceive(commandResultQueue_, &commandResult, 0) == pdTRUE)
        {
            const uint8_t actor = static_cast<uint8_t>(commandResult.type);
            portENTER_CRITICAL(&commandLedgerMux_);
            commandLedger_.update(actor,
                                  commandResult.commandId,
                                  commandResult.stage,
                                  commandResult.result);
            portEXIT_CRITICAL(&commandLedgerMux_);

            uint8_t peer[6]{};
            if (snapshotPeer(peer))
            {
                sendCommandAck(peer,
                               commandResult.commandId,
                               commandResult.stage,
                               commandResult.result,
                               commandResult.errorCode);
            }
        }

        const uint32_t now = millis();
        const uint32_t lastRx = lastRxMs_.load();
        const heatpump::BridgeWatchdog::Transition bridgeTransition =
            bridgeWatchdog_.update(now, lastRx);
        if (bridgeTransition.requestSafetyStop)
        {
            requestSafetyStop();
        }
        const bool bridgeOnline = bridgeWatchdog_.online();

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
                bridgeOnline ? 1U : 0U,
                static_cast<unsigned long>(lastRx == 0U ? 0U : now - lastRx),
                static_cast<unsigned long>(lastTxMs_ == 0 ? 0 : now - lastTxMs_),
                static_cast<int>(lastSendResult_),
                static_cast<unsigned>(lastSendStatus_));
        }
    }

    poolwire::HeatPumpTelemetry EspNowBridge::makeStatusPayload() const
    {
        const heatpump::HeatPumpData snapshot = state_.snapshot();

        poolwire::HeatPumpTelemetry payload{};
        payload.waterTemperatureCentiDegrees = poolwire::temperatureToCentiDegrees(snapshot.waterTemperature);
        payload.setpointCentiDegrees = poolwire::temperatureToCentiDegrees(snapshot.targetTemperature);
        payload.currentMilliAmps = poolwire::amperesToMilliAmps(snapshot.currentRMS);
        payload.errorCode = snapshot.errorCode;
        payload.mode = static_cast<poolwire::HeatPumpMode>(snapshot.mode);
        payload.waveformCount = static_cast<uint8_t>(poolwire::kWaveformSamples);

        if (snapshot.powerOn) payload.flags |= poolwire::HeatPumpPowerOn;
        if (snapshot.powerStateValid) payload.flags |= poolwire::HeatPumpPowerValid;
        if (snapshot.compressorRunning) payload.flags |= poolwire::CompressorRunning;
        if (snapshot.errorActive) payload.flags |= poolwire::ErrorActive;
        if (snapshot.currentClipping) payload.flags |= poolwire::CurrentClipping;

        for (size_t i = 0; i < poolwire::kWaveformSamples; ++i)
        {
            payload.waveformCentiAmps[i] = poolwire::amperesToCentiAmps(snapshot.waveform[i]);
        }

        const uint32_t now = millis();
        if (snapshot.lastNetFrameMs != 0 &&
            (now - snapshot.lastNetFrameMs) <= config::control::kNetConnectionTimeoutMs)
        {
            payload.flags |= poolwire::NetBusConnected;
        }

        return payload;
    }

    void EspNowBridge::sendStatus()
    {
        if (!config::espnow::kEnableStatusSend)
        {
            return;
        }

        uint8_t peer[6]{};
        const bool peerValid = snapshotPeer(peer);

        if (!peerValid && !config::espnow::kBroadcastStatusWhenNoPeer)
        {
            return;
        }

        const uint8_t* target = peerValid ? peer : kBroadcastMac;
        const poolwire::NodeId destination = peerValid
            ? poolwire::NodeId::EspNowBridge
            : poolwire::NodeId::Broadcast;
        const poolwire::HeatPumpTelemetry payload = makeStatusPayload();
        poolwire::EncodedFrame frame{};

        if (!poolwire::encodeMessage(nextEnvelope(destination), payload, frame))
        {
            lastSendResult_ = ESP_ERR_INVALID_ARG;
            return;
        }

        lastSendResult_ = esp_now_send(target, frame.data(), frame.size);

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

    void EspNowBridge::onRecvThunk(const esp_now_recv_info_t* info, const uint8_t* data, const int len)
    {
        if (instance_ != nullptr && info != nullptr && info->src_addr != nullptr)
        {
            instance_->onRecv(info->src_addr, data, len);
        }
    }

    bool EspNowBridge::snapshotPeer(uint8_t out[6]) const
    {
        if (out == nullptr) return false;
        bool valid = false;
        portENTER_CRITICAL(&stateMux_);
        valid = peerValid_;
        if (valid) std::memcpy(out, peerMac_, 6);
        portEXIT_CRITICAL(&stateMux_);
        return valid;
    }

    void EspNowBridge::requestSafetyStop()
    {
        if (commandQueue_ == nullptr) return;

        heatpump::HeatPumpCommand command{};
        command.type = heatpump::HeatPumpCommandType::SafetyStop;
        command.powerOn = false;
        command.localExpiresAtMs = UINT32_MAX;

        if (xQueueSendToFront(commandQueue_, &command, 0U) == pdTRUE)
        {
            logger::Logger::log(logger::Type::Comms,
                                "bridge timeout: safety stop queued");
        }
    }

    void EspNowBridge::onSentThunk(const wifi_tx_info_t* info, const esp_now_send_status_t status)
    {
        (void)info;

        if (instance_ != nullptr)
        {
            instance_->onSent(nullptr, status);
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

        poolwire::DecodeOptions options{};
        options.localNode = poolwire::NodeId::HeatPump;
        options.expectedSource = poolwire::NodeId::EspNowBridge;

        poolwire::FrameView frame{};

        if (poolwire::decodeFrame(data, static_cast<size_t>(len), options, frame) != poolwire::DecodeStatus::Ok)
        {
            return;
        }

        uint8_t configuredPeer[6]{};
        if (snapshotPeer(configuredPeer) &&
            std::memcmp(configuredPeer, mac, 6) != 0 &&
            frame.header.messageType != poolwire::MessageType::Discovery)
        {
            return;
        }

        if (frame.header.messageType == poolwire::MessageType::Discovery)
        {
            poolwire::Discovery discovery{};

            if (poolwire::decodeMessage(frame, discovery) != poolwire::DecodeStatus::Ok)
            {
                return;
            }

            receivedSequences_.reset(frame.header.source);

            if (receivedSequences_.accept(frame.header.source, frame.header.sequenceNumber) != poolwire::DecodeStatus::Ok)
            {
                return;
            }

            lastRxMs_.store(millis());
            rememberPeer(mac);
            sendPairing(mac, poolwire::PairingAction::Offer);
            return;
        }

        if (frame.header.messageType == poolwire::MessageType::Pairing)
        {
            poolwire::Pairing pairing{};

            if (poolwire::decodeMessage(frame, pairing) != poolwire::DecodeStatus::Ok ||
                std::memcmp(pairing.mac.data(), mac, pairing.mac.size()) != 0)
            {
                return;
            }

            if (pairing.action == poolwire::PairingAction::Offer)
            {
                receivedSequences_.reset(frame.header.source);
            }

            if (receivedSequences_.accept(frame.header.source, frame.header.sequenceNumber) != poolwire::DecodeStatus::Ok)
            {
                return;
            }

            lastRxMs_.store(millis());
            rememberPeer(mac);

            if (pairing.action == poolwire::PairingAction::Offer)
            {
                sendPairing(mac, poolwire::PairingAction::Confirm);
            }
            return;
        }

        if (frame.header.messageType == poolwire::MessageType::NodeStatus)
        {
            poolwire::NodeStatus status{};
            if (poolwire::decodeMessage(frame, status) != poolwire::DecodeStatus::Ok ||
                receivedSequences_.accept(frame.header.source,
                                          frame.header.sequenceNumber) != poolwire::DecodeStatus::Ok)
            {
                return;
            }

            lastRxMs_.store(millis());
            rememberPeer(mac);
            return;
        }

        if (frame.header.messageType == poolwire::MessageType::HeatPumpCommand)
        {
            poolwire::HeatPumpCommand payload{};

            if (poolwire::decodeMessage(frame, payload) != poolwire::DecodeStatus::Ok)
            {
                sendCommandAck(mac,
                               frame.header.commandId,
                               poolwire::CommandAckStage::Rejected,
                               poolwire::AckResult::InvalidPayload,
                               1U);
                return;
            }

            logger::Logger::log(logger::Type::Comms,
                                "ESP-NOW command RX frameBytes=%d payloadBytes=%u wireSequence=%lu metadataSequence=%lu commandId=%lu messageType=%u command=%u power=%u setpointCenti=%d mode=%u validityMs=%lu",
                                len,
                                static_cast<unsigned>(frame.header.payloadLength),
                                static_cast<unsigned long>(frame.header.sequenceNumber),
                                static_cast<unsigned long>(payload.metadata.sequenceNumber),
                                static_cast<unsigned long>(frame.header.commandId),
                                static_cast<unsigned>(frame.header.messageType),
                                static_cast<unsigned>(payload.command),
                                static_cast<unsigned>(payload.power),
                                static_cast<int>(payload.setpointCentiDegrees),
                                static_cast<unsigned>(payload.mode),
                                static_cast<unsigned long>(payload.metadata.validityMs));

            const poolwire::DecodeStatus sequenceStatus =
                receivedSequences_.accept(frame.header.source, frame.header.sequenceNumber);

            if (sequenceStatus != poolwire::DecodeStatus::Ok &&
                sequenceStatus != poolwire::DecodeStatus::Duplicate)
            {
                return;
            }

            const uint8_t actor = static_cast<uint8_t>(payload.command);

            if (sequenceStatus == poolwire::DecodeStatus::Duplicate)
            {
                poolwire::CommandLedgerEntry entry{};
                bool found = false;
                portENTER_CRITICAL(&commandLedgerMux_);
                if (const poolwire::CommandLedgerEntry* stored =
                        commandLedger_.find(actor, frame.header.commandId))
                {
                    entry = *stored;
                    found = true;
                }
                portEXIT_CRITICAL(&commandLedgerMux_);

                if (found)
                {
                    sendCommandAck(mac,
                                   frame.header.commandId,
                                   entry.stage,
                                   entry.result);
                }
                return;
            }

            poolwire::CommandObservation observation{};
            poolwire::CommandLedgerEntry ledgerEntry{};
            portENTER_CRITICAL(&commandLedgerMux_);
            observation = commandLedger_.observe(actor,
                                                 frame.header.commandId,
                                                 payload.metadata,
                                                 millis());
            if (const poolwire::CommandLedgerEntry* stored =
                    commandLedger_.find(actor, frame.header.commandId))
            {
                ledgerEntry = *stored;
            }
            portEXIT_CRITICAL(&commandLedgerMux_);

            if (observation == poolwire::CommandObservation::Duplicate)
            {
                sendCommandAck(mac,
                               frame.header.commandId,
                               ledgerEntry.stage,
                               ledgerEntry.result);
                return;
            }

            if (observation != poolwire::CommandObservation::NewCommand)
            {
                const poolwire::AckResult result = observation == poolwire::CommandObservation::Superseded
                    ? poolwire::AckResult::Superseded
                    : poolwire::AckResult::Expired;
                sendCommandAck(mac,
                               frame.header.commandId,
                               poolwire::CommandAckStage::Rejected,
                               result,
                               2U);
                return;
            }

            heatpump::HeatPumpCommand command{};

            if (!convertCommandPayload(payload, command))
            {
                portENTER_CRITICAL(&commandLedgerMux_);
                commandLedger_.update(actor,
                                      frame.header.commandId,
                                      poolwire::CommandAckStage::Rejected,
                                      poolwire::AckResult::InvalidPayload);
                portEXIT_CRITICAL(&commandLedgerMux_);
                sendCommandAck(mac,
                               frame.header.commandId,
                               poolwire::CommandAckStage::Rejected,
                               poolwire::AckResult::InvalidPayload,
                               3U);
                return;
            }

            command.commandId = frame.header.commandId;
            command.sequenceNumber = payload.metadata.sequenceNumber;
            command.localExpiresAtMs = millis() + payload.metadata.validityMs;

            lastRxMs_.store(millis());
            rememberPeer(mac);
            sendCommandAck(mac,
                           frame.header.commandId,
                           poolwire::CommandAckStage::ReceivedByNode,
                           poolwire::AckResult::Accepted);
            const bool queued = injectCommand(command);

            if (!queued)
            {
                portENTER_CRITICAL(&commandLedgerMux_);
                commandLedger_.update(actor,
                                      frame.header.commandId,
                                      poolwire::CommandAckStage::Rejected,
                                      poolwire::AckResult::QueueFull);
                portEXIT_CRITICAL(&commandLedgerMux_);
                sendCommandAck(mac,
                               frame.header.commandId,
                               poolwire::CommandAckStage::Rejected,
                               poolwire::AckResult::QueueFull,
                               4U);
            }

            logger::Logger::log(
                logger::Type::Comms,
                "ESP-NOW heatpump command received type=%u queued=%u",
                static_cast<unsigned>(payload.command),
                queued ? 1U : 0U);
            return;
        }
    }

    void EspNowBridge::sendPairing(const uint8_t* mac, const poolwire::PairingAction action)
    {
        if (mac == nullptr)
        {
            return;
        }

        addPeer(mac);

        poolwire::Pairing pairing{};
        pairing.action = action;
        pairing.channel = config::espnow::kWifiChannel;
        esp_wifi_get_mac(WIFI_IF_STA, pairing.mac.data());

        poolwire::EncodedFrame frame{};

        if (!poolwire::encodeMessage(
                nextEnvelope(poolwire::NodeId::EspNowBridge),
                pairing,
                frame))
        {
            return;
        }

        const esp_err_t result = esp_now_send(mac, frame.data(), frame.size);

        if (result != ESP_OK)
        {
            logger::Logger::log(logger::Type::Comms, "pairing send failed err=%d", static_cast<int>(result));
        }
    }

    void EspNowBridge::sendCommandAck(const uint8_t* mac,
                                      const uint32_t commandId,
                                      const poolwire::CommandAckStage stage,
                                      const poolwire::AckResult result,
                                      const uint16_t errorCode)
    {
        if (mac == nullptr)
        {
            return;
        }

        poolwire::CommandAck ack{};
        ack.acknowledgedType = poolwire::MessageType::HeatPumpCommand;
        ack.stage = stage;
        ack.result = result;
        ack.errorCode = errorCode;

        poolwire::EncodedFrame frame{};

        if (poolwire::encodeMessage(
                nextEnvelope(poolwire::NodeId::EspNowBridge, commandId),
                ack,
                frame))
        {
            esp_now_send(mac, frame.data(), frame.size);
        }
    }

    bool EspNowBridge::convertCommandPayload(const poolwire::HeatPumpCommand& payload,
                                             heatpump::HeatPumpCommand& outCommand)
    {
        using ControllerCommandType = heatpump::HeatPumpCommandType;

        switch (payload.command)
        {
            case poolwire::HeatPumpCommandKind::Power:
                outCommand.type = ControllerCommandType::Power;
                outCommand.powerOn = payload.power == poolwire::RelayState::On;
                return true;

            case poolwire::HeatPumpCommandKind::Setpoint:
                outCommand.type = ControllerCommandType::SetTemperature;
                outCommand.targetTemperature = poolwire::centiDegreesToTemperature(payload.setpointCentiDegrees);
                return true;

            case poolwire::HeatPumpCommandKind::Mode:
                outCommand.type = ControllerCommandType::SetMode;

                switch (payload.mode)
                {
                    case poolwire::HeatPumpMode::Heat:
                        outCommand.mode = heatpump::HeatPumpMode::Heat;
                        return true;

                    case poolwire::HeatPumpMode::Cool:
                        outCommand.mode = heatpump::HeatPumpMode::Cool;
                        return true;

                    case poolwire::HeatPumpMode::Auto:
                        outCommand.mode = heatpump::HeatPumpMode::Auto;
                        return true;

                    default:
                        return false;
                }

            case poolwire::HeatPumpCommandKind::RequestStatus:
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
