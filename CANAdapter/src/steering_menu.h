#pragma once
#include <stdint.h>
// Return true when a menu gesture owns this input (including its release).
bool handleSteeringMenu(uint8_t code, uint32_t now);
void processSteeringMenu(uint32_t now, bool windowsIdle);
// Implemented by the UART owner. False means retry without dropping the event.
bool sendDashMenu(uint8_t state, int8_t adjustment);
