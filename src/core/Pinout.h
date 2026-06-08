#pragma once

#include <Arduino.h>

namespace PetBionicsPinout
{
    constexpr uint8_t kSpiSck = D6;
    constexpr uint8_t kSpiMiso = D5;
    constexpr uint8_t kSpiMosi = D4;
    constexpr uint8_t kImuCs = D7;
    constexpr uint8_t kSdCs = D8;
    constexpr uint8_t kHx711Dout = D10;
    constexpr uint8_t kHx711Sck = D9;
} // namespace PetBionicsPinout