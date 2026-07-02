#pragma once

#include <Arduino.h>
#include <initializer_list>

void sendCANFrame(uint32_t canID, const uint8_t* data, uint8_t dataLength, bool extended = false, bool rtr = false);
void sendCANFrame(uint32_t canID, std::initializer_list<uint8_t> data, bool extended = false, bool rtr = false);
