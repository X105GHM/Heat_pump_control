#pragma once
#include <cstdint>

namespace config::pins
{
    static constexpr uint8_t kNetBus        = 4;    // Single-wire NET bus, never drive HIGH
    static constexpr uint8_t kCurrentAdc    = 5;    // ADC input for SCT-013-030 conditioning circuit
    static constexpr uint8_t kStatusLed     = 9;    // Heat pump / compressor status
    static constexpr uint8_t kTxLed         = 10;   // NET bus TX activity
    static constexpr uint8_t kRxLed         = 11;   // NET bus RX activity
}
