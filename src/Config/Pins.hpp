#pragma once
#include <cstdint>

#ifndef LED_PIN
#define LED_PIN 48
#endif

namespace config::pins
{
    static constexpr uint8_t kNetBus    = 4;   // Single-wire NET bus, never drive HIGH
    static constexpr uint8_t kCurrentAdc = 5;  // ADC1 input on ESP32-S3
    static constexpr uint8_t kStatusLed  = 9;  // Heat pump / compressor status
    static constexpr uint8_t kTxLed      = 11; // NET bus TX activity
    static constexpr uint8_t kRxLed      = 10; // NET bus RX activity
    static constexpr uint8_t kErrorLed   = LED_PIN; // Onboard NeoPixel
}
