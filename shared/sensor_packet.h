#ifndef SENSOR_PACKET_H
#define SENSOR_PACKET_H

#include <stddef.h>
#include <stdint.h>

// UART frame: 0xAA | sizeof(PayloadF) | payload | XOR(payload).
// Floats and ebar use little-endian order on both ESP32 endpoints.
// The widened ebar changes the payload length from 59 to 60 bytes. Receivers
// reject other lengths, so CANAdapter and DashDisplay must be updated together.
#pragma pack(push, 1)
struct PayloadF {
  uint8_t seq;
  float rpm;
  float hv_current_A;
  float hv_voltage_V;
  float ect_C;
  float hv_intake_C;
  float tb1_C;
  float tb2_C;
  float tb3_C;
  float soc_pct;
  float mg1_temp_F;
  float mg1_rpm;
  float mg2_temp_F;
  float mg2_rpm;
  int16_t ebar;
  uint8_t est;
  uint8_t bfs;
  uint8_t bfor;
  uint8_t dim;
  uint8_t off;
};
#pragma pack(pop)

static_assert(sizeof(float) == 4, "UART requires 32-bit floats");
static_assert(sizeof(PayloadF) == 60, "UART payload layout changed");
static_assert(offsetof(PayloadF, ebar) == 53, "UART ebar offset changed");
static_assert(offsetof(PayloadF, est) == 55, "UART trailing fields shifted");
static_assert(offsetof(PayloadF, off) == 59, "UART off offset changed");

inline void writeSensorInt16LE(uint8_t* destination, int16_t value) {
  const uint16_t bits = static_cast<uint16_t>(value);
  destination[0] = static_cast<uint8_t>(bits);
  destination[1] = static_cast<uint8_t>(bits >> 8);
}

#endif
