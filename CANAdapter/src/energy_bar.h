#ifndef ENERGY_BAR_H
#define ENERGY_BAR_H

#include <stdint.h>

struct EnergyBarReading {
  int16_t value;  // Full decoded value; only renderers may saturate their bars.
  bool valid;
};

// Capture-supported 0x247 decode: byte 0 bits 1:0 extend byte 1.
inline EnergyBarReading decodeEnergyBar(const uint8_t* data, uint8_t length) {
  if (data == nullptr || length < 3 || data[2] != 0x32) {
    return {0, false};
  }

  int16_t raw = static_cast<int16_t>(((data[0] & 0x03) << 8) | data[1]);
  if (raw & 0x200) raw -= 1024;
  if (raw == -512) return {0, false};

  return {raw, true};
}

#endif
