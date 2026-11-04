#ifndef MATRIX_PROTOCOL_H
#define MATRIX_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

namespace matrix_indicator {
constexpr uint8_t ENGINE = 1 << 0;
constexpr uint8_t DIM = 1 << 1;
constexpr uint8_t EV_MODE = 1 << 2;
constexpr uint8_t OFF = 1 << 3;
// Preserve direction/activity even when a small value quantizes to zero LEDs.
constexpr uint8_t REGEN = 1 << 4;
constexpr uint8_t ACTIVE = 1 << 5;
constexpr uint8_t POWER = 1 << 6; // Demand above the car display's 100-unit boundary.

struct State {
  uint8_t flags;
  int8_t step;
};
static_assert(sizeof(State) == 2, "Renderer state must remain two bytes");

// The same full decoded number sent to DashDisplay. No scaling or clamp.
struct WireState {
  uint8_t flags;
  int16_t ebar;
};
constexpr size_t PACKET_SIZE = 3; // flags, signed ebar low byte, high byte

inline WireState makeWireState(float rpm, int16_t ebar, bool dim, bool evMode, bool off) {
  const uint8_t flags = (rpm > 500.0f ? ENGINE : 0) | (dim ? DIM : 0) |
      (evMode ? EV_MODE : 0) | (off ? OFF : 0) |
      (ebar < 0 ? REGEN : 0) | (ebar != 0 ? ACTIVE : 0) |
      (ebar > 100 ? POWER : 0);
  return {flags, ebar};
}

inline void encodePacket(WireState value, uint8_t* data) {
  data[0] = value.flags;
  const uint16_t bits = static_cast<uint16_t>(value.ebar);
  data[1] = static_cast<uint8_t>(bits);
  data[2] = static_cast<uint8_t>(bits >> 8);
}

inline bool decodePacket(const uint8_t* data, size_t length, WireState& value) {
  if (!data || length != PACKET_SIZE) return false;
  const int32_t bits = static_cast<int32_t>(data[1]) | (static_cast<int32_t>(data[2]) << 8);
  value = {static_cast<uint8_t>(data[0] & 0x7F),
      static_cast<int16_t>(bits >= 32768 ? bits-65536 : bits)};
  return true;
}

// Quantize exactly once, after selecting the 50-unit range. All seventeen
// lengths (0 through 16 pixels) remain reachable in both EV and GAS.
inline State displayState(WireState wire) {
  const int value = wire.ebar;
  if (value < 0) {
    const int magnitude = value < -100 ? 100 : -value;
    return {wire.flags,static_cast<int8_t>(-magnitude*16/100)};
  }
  int selected = value - ((wire.flags & ENGINE) ? 50 : 0);
  if (selected < 0) selected = 0;
  if (selected > 50) selected = 50;
  return {wire.flags,static_cast<int8_t>((selected*16+25)/50)};
}

inline State makeState(float rpm, int16_t ebar, bool dim, bool evMode, bool off) {
  return displayState(makeWireState(rpm,ebar,dim,evMode,off));
}

inline bool regen(State s) { return (s.flags & REGEN) || s.step < 0; }
inline bool active(State s) { return (s.flags & ACTIVE) || s.step != 0; }
inline uint8_t brightness(State s) { return (s.flags & OFF) ? 0 : (s.flags & DIM) ? 10 : 255; }
}  // namespace matrix_indicator
#endif
