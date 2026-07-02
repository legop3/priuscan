#include "steering_controls.h"

#include "can_tx.h"

namespace {

constexpr unsigned long BACK_HORN_HOLD_MS = 200;

constexpr unsigned long WINDOW_HOLD_TO_MOVE_MS = 150;
constexpr unsigned long WINDOW_MOVE_REFRESH_MS = 750;
constexpr unsigned long WINDOW_ACK_TIMEOUT_MS = 60;
constexpr uint8_t WINDOW_MAX_RETRIES = 2;

constexpr uint8_t WINDOW_CMD_STOP = 0x00;
constexpr uint8_t WINDOW_CMD_DOWN = 0x40;
constexpr uint8_t WINDOW_CMD_UP = 0x80;

uint8_t currentSteerCode = STEER_NONE;
unsigned long currentSteerCodeStartMs = 0;

bool backHoldActive = false;
unsigned long backHoldStartMs = 0;
bool wirelessBuzzerOn = false;

bool windowMoveHoldActive = false;
uint8_t windowMoveHoldCmd = WINDOW_CMD_STOP;
unsigned long windowLastMoveRequestMs = 0;

bool windowGroupTxActive = false;
uint8_t windowGroupCmd = WINDOW_CMD_STOP;
uint8_t windowGroupSubs[4] = {0};
uint8_t windowGroupSubCount = 0;
uint8_t windowGroupSubIndex = 0;
bool windowGroupAwaitingAck = false;
uint8_t windowGroupRetryCount = 0;
unsigned long windowGroupLastSendMs = 0;

bool windowPendingGroupActive = false;
uint8_t windowPendingGroupCmd = WINDOW_CMD_STOP;

void sendWirelessBuzzerCommand(bool on) {
    if (wirelessBuzzerOn == on) return;
    wirelessBuzzerOn = on;
    if (on) {
        sendCANFrame(0x750, {0x40,0x04,0x30,0x14,0x00,0x80,0x00,0x00});
    } else {
        sendCANFrame(0x750, {0x40,0x04,0x30,0x14,0x00,0x00,0x00,0x00});
    }
}

void sendWindowCommand(uint8_t sub, uint8_t cmd) {
    sendCANFrame(0x750, {sub,0x04,0x30,0x01,0x01,cmd,0x00,0x00});
}

uint8_t buildWindowGroupSubs(uint8_t* outSubs) {
    outSubs[0] = 0x90;
    outSubs[1] = 0x91;
    outSubs[2] = 0x93;
    outSubs[3] = 0x92;
    return 4;
}

void startWindowGroupCommand(uint8_t cmd, unsigned long now) {
    windowGroupCmd = cmd;
    windowGroupSubCount = buildWindowGroupSubs(windowGroupSubs);
    windowGroupSubIndex = 0;
    windowGroupAwaitingAck = false;
    windowGroupRetryCount = 0;
    windowGroupLastSendMs = now;
    windowGroupTxActive = (windowGroupSubCount > 0);
}

void queueWindowGroupCommand(uint8_t cmd, unsigned long now, bool replacePending) {
    if (!windowGroupTxActive) {
        startWindowGroupCommand(cmd, now);
        return;
    }

    if (replacePending || !windowPendingGroupActive || windowPendingGroupCmd != cmd) {
        windowPendingGroupCmd = cmd;
        windowPendingGroupActive = true;
    }
}

void startPendingWindowGroupIfAny(unsigned long now) {
    if (!windowPendingGroupActive) return;

    const uint8_t cmd = windowPendingGroupCmd;
    windowPendingGroupActive = false;
    startWindowGroupCommand(cmd, now);
}

void finishWindowGroup(unsigned long now) {
    windowGroupTxActive = false;
    windowGroupAwaitingAck = false;
    startPendingWindowGroupIfAny(now);
}

void advanceWindowGroupAfterCurrentSub(unsigned long now) {
    windowGroupRetryCount = 0;
    windowGroupAwaitingAck = false;

    windowGroupSubIndex++;
    if (windowGroupSubIndex >= windowGroupSubCount) {
        finishWindowGroup(now);
    }
}

void processWindowGroupTx(unsigned long now) {
    if (!windowGroupTxActive) return;

    if (!windowGroupAwaitingAck) {
        sendWindowCommand(windowGroupSubs[windowGroupSubIndex], windowGroupCmd);
        windowGroupAwaitingAck = true;
        windowGroupLastSendMs = now;
        return;
    }

    if ((now - windowGroupLastSendMs) < WINDOW_ACK_TIMEOUT_MS) return;

    if (windowGroupRetryCount < WINDOW_MAX_RETRIES) {
        windowGroupRetryCount++;
        sendWindowCommand(windowGroupSubs[windowGroupSubIndex], windowGroupCmd);
        windowGroupLastSendMs = now;
        return;
    }

    // Give up on this subaddress and continue with the next one.
    advanceWindowGroupAfterCurrentSub(now);
}

void onWindowAck(uint8_t sub, unsigned long now) {
    if (!windowGroupTxActive || !windowGroupAwaitingAck) return;
    if (windowGroupSubIndex >= windowGroupSubCount) return;
    if (sub != windowGroupSubs[windowGroupSubIndex]) return;

    advanceWindowGroupAfterCurrentSub(now);
}

void processWirelessHorn(unsigned long now) {
    if (backHoldActive && !wirelessBuzzerOn && (now - backHoldStartMs) >= BACK_HORN_HOLD_MS) {
        sendWirelessBuzzerCommand(true);
    }

    if (!backHoldActive && wirelessBuzzerOn) {
        sendWirelessBuzzerCommand(false);
    }
}

void queueWindowStopIfMoving(unsigned long now) {
    if (!windowMoveHoldActive) return;

    windowMoveHoldActive = false;
    windowMoveHoldCmd = WINDOW_CMD_STOP;
    windowLastMoveRequestMs = 0;
    windowPendingGroupActive = false;
    queueWindowGroupCommand(WINDOW_CMD_STOP, now, true);
}

void processWindowControl(unsigned long now) {
    const bool holdingUp = (currentSteerCode == STEER_UP);
    const bool holdingDown = (currentSteerCode == STEER_DOWN);
    const bool holdingDir = holdingUp || holdingDown;

    if (!holdingDir) {
        queueWindowStopIfMoving(now);
        return;
    }

    const unsigned long heldMs = now - currentSteerCodeStartMs;
    if (heldMs < WINDOW_HOLD_TO_MOVE_MS) {
        return;
    }

    const uint8_t cmd = holdingUp ? WINDOW_CMD_UP : WINDOW_CMD_DOWN;

    if (!windowMoveHoldActive || windowMoveHoldCmd != cmd) {
        windowMoveHoldActive = true;
        windowMoveHoldCmd = cmd;
        windowLastMoveRequestMs = now;
        queueWindowGroupCommand(cmd, now, false);
        return;
    }

    if ((now - windowLastMoveRequestMs) >= WINDOW_MOVE_REFRESH_MS) {
        windowLastMoveRequestMs = now;
        queueWindowGroupCommand(cmd, now, false);
    }
}

} // namespace

void handleBodyAckFrame(const CAN_FRAME& can_message) {
    // Positive response for window command looks like:
    // SS 02 70 01 00 00 00 00
    if (can_message.length < 4) return;
    const uint8_t* d = can_message.data.byte;
    if (d[2] == 0x70 && d[3] == 0x01) {
        onWindowAck(d[0], millis());
    }
}

void handleSteeringButton(uint8_t code, unsigned long now) {
    if (code == currentSteerCode) {
        return;
    }

    const uint8_t prev = currentSteerCode;
    currentSteerCode = code;
    currentSteerCodeStartMs = now;
    const bool pressEdge = (prev == STEER_NONE && code != STEER_NONE);
    const bool releaseEdge = (prev != STEER_NONE && code == STEER_NONE);

    if (pressEdge && code == STEER_BACK) {
        backHoldActive = true;
        backHoldStartMs = now;
    }

    if (releaseEdge && prev == STEER_BACK) {
        backHoldActive = false;
        sendWirelessBuzzerCommand(false);
    }

    if (releaseEdge && (prev == STEER_UP || prev == STEER_DOWN)) {
        queueWindowStopIfMoving(now);
    }
}

void processSteeringControlState(unsigned long now) {
    processWirelessHorn(now);
    processWindowGroupTx(now);
    processWindowControl(now);
}

bool isWindowMotionBusy() {
    return windowGroupTxActive || windowPendingGroupActive || windowMoveHoldActive;
}
