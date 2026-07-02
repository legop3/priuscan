#pragma once

#include <Arduino.h>
#include <esp32_can.h>

// Steering-wheel button decode on 0x58E D6.
enum : uint8_t {
    STEER_NONE  = 0x00,
    STEER_LEFT  = 0x01,
    STEER_RIGHT = 0x02,
    STEER_UP    = 0x03,
    STEER_DOWN  = 0x04,
    STEER_ENTER = 0x05,
    STEER_BACK  = 0x06
};

void handleSteeringButton(uint8_t code, unsigned long now);
void handleBodyAckFrame(const CAN_FRAME& can_message);
void processSteeringControlState(unsigned long now);
bool isWindowMotionBusy();
