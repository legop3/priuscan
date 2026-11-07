#include "steering_controls.h"
#include "steering_menu.h"

#include "can_tx.h"

#include <deque>

namespace {

constexpr unsigned long BACK_HORN_HOLD_MS = 200;
constexpr unsigned long WINDOW_MOVE_REFRESH_MS = 750;
// Response timeout only; this does not set a minimum movement duration.
constexpr unsigned long WINDOW_ACK_TIMEOUT_MS = 250;
constexpr uint8_t WINDOW_MAX_RETRIES = 2;
constexpr uint8_t WINDOW_SUBS[] = {0x90, 0x91, 0x93, 0x92};
constexpr uint8_t WINDOW_COUNT = sizeof(WINDOW_SUBS) / sizeof(WINDOW_SUBS[0]);

constexpr uint8_t WINDOW_CMD_STOP = 0x00;
constexpr uint8_t WINDOW_CMD_DOWN = 0x40;
constexpr uint8_t WINDOW_CMD_UP = 0x80;

uint8_t currentSteerCode = STEER_NONE;
bool backHoldActive = false;
unsigned long backHoldStartMs = 0;
bool wirelessBuzzerOn = false;

struct WindowGesture {
    uint8_t cmd;
    unsigned long pressedMs;
    unsigned long durationMs;
    bool released;
};

struct WindowMotion {
    bool started = false;
    bool stopped = false;
    bool stopComplete = false;
    unsigned long startedMs = 0;
    unsigned long lastMoveMs = 0;
};

// Preserve complete taps received while the previous tap is finishing.
std::deque<WindowGesture> windowGestures;
WindowMotion windowMotions[WINDOW_COUNT];
// Only one diagnostic request may be outstanding through the body gateway.
// Its ACK permits the next command, while previously started motors keep moving.
struct WindowTransaction {
    bool active = false;
    uint8_t index = 0;
    uint8_t cmd = WINDOW_CMD_STOP;
    uint8_t retries = 0;
    unsigned long sentMs = 0;
};
WindowTransaction windowTx;

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

void beginWindowTransaction(uint8_t index, uint8_t cmd, unsigned long now) {
    windowTx.active = true;
    windowTx.index = index;
    windowTx.cmd = cmd;
    windowTx.retries = 0;
    windowTx.sentMs = now;
    sendWindowCommand(WINDOW_SUBS[index], cmd);
}

void finishWindowTransaction() {
    if (windowTx.cmd == WINDOW_CMD_STOP) {
        windowMotions[windowTx.index].stopComplete = true;
    }
    windowTx.active = false;
}

void releaseWindowGesture(unsigned long now) {
    if (windowGestures.empty() || windowGestures.back().released) return;
    WindowGesture& gesture = windowGestures.back();
    gesture.durationMs = now - gesture.pressedMs;
    gesture.released = true;
}

void processWindowControl(unsigned long now) {
    if (windowGestures.empty()) return;
    const WindowGesture& gesture = windowGestures.front();

    // Pace starts, refreshes AND stops by the gateway's response. Do not
    // overwrite an outstanding request with a burst of commands for other doors.
    if (windowTx.active) {
        if (now - windowTx.sentMs < WINDOW_ACK_TIMEOUT_MS) return;
        if (windowTx.retries < WINDOW_MAX_RETRIES) {
            const WindowMotion& motion = windowMotions[windowTx.index];
            const bool moveExpired = windowTx.cmd != WINDOW_CMD_STOP &&
                gesture.released && now - motion.startedMs >= gesture.durationMs;
            if (!moveExpired) {
                ++windowTx.retries;
                windowTx.sentMs = now;
                sendWindowCommand(WINDOW_SUBS[windowTx.index], windowTx.cmd);
                if (windowTx.cmd != WINDOW_CMD_STOP) {
                    windowMotions[windowTx.index].lastMoveMs = now;
                }
                return;
            }
            // Once its time is up, send stop instead of retrying movement.
        }
        // Bound the wait if an ECU does not reply.
        finishWindowTransaction();
    }

    // A due stop takes priority over another start or refresh. Every window
    // still receives the complete tap, measured from its own move command.
    for (uint8_t i = 0; i < WINDOW_COUNT; ++i) {
        WindowMotion& motion = windowMotions[i];
        if (motion.started && !motion.stopped && gesture.released &&
            now - motion.startedMs >= gesture.durationMs) {
            motion.stopped = true;
            beginWindowTransaction(i, WINDOW_CMD_STOP, now);
            return;
        }
    }

    for (uint8_t i = 0; i < WINDOW_COUNT; ++i) {
        WindowMotion& motion = windowMotions[i];
        if (motion.started && !motion.stopped &&
            now - motion.lastMoveMs >= WINDOW_MOVE_REFRESH_MS) {
            motion.lastMoveMs = now;
            beginWindowTransaction(i, gesture.cmd, now);
            return;
        }
    }

    for (uint8_t i = 0; i < WINDOW_COUNT; ++i) {
        WindowMotion& motion = windowMotions[i];
        if (!motion.started) {
            motion.started = true;
            motion.startedMs = now;
            motion.lastMoveMs = now;
            beginWindowTransaction(i, gesture.cmd, now);
            return;
        }
    }

    for (const WindowMotion& motion : windowMotions) {
        if (!motion.stopComplete) return;
    }
    windowGestures.pop_front();
    for (WindowMotion& motion : windowMotions) motion = WindowMotion{};
}

void processWirelessHorn(unsigned long now) {
    if (backHoldActive && !wirelessBuzzerOn && (now - backHoldStartMs) >= BACK_HORN_HOLD_MS) {
        sendWirelessBuzzerCommand(true);
    }
    if (!backHoldActive && wirelessBuzzerOn) {
        sendWirelessBuzzerCommand(false);
    }
}

} // namespace

void handleBodyAckFrame(const CAN_FRAME& can_message) {
    if (can_message.length < 4) return;
    const uint8_t* d = can_message.data.byte;
    if (d[1] != 0x02 || d[2] != 0x70 || d[3] != 0x01) return;
    if (!windowTx.active || d[0] != WINDOW_SUBS[windowTx.index]) return;
    finishWindowTransaction();
}

void handleSteeringButton(uint8_t code, unsigned long now) {
    if (code == currentSteerCode) return;

    const uint8_t prev = currentSteerCode;
    currentSteerCode = code;

    if (prev == STEER_BACK) {
        backHoldActive = false;
        sendWirelessBuzzerCommand(false);
    }
    // Always release prior car-control gestures before changing ownership.
    if (prev == STEER_UP || prev == STEER_DOWN) releaseWindowGesture(now);
    if (handleSteeringMenu(code,now)) return;
    if (code == STEER_BACK) {
        backHoldActive = true;
        backHoldStartMs = now;
    }

    if (code == STEER_UP || code == STEER_DOWN) {
        windowGestures.push_back({code == STEER_UP ? WINDOW_CMD_UP : WINDOW_CMD_DOWN,
                                  now, 0, false});
    }
}

void processSteeringControlState(unsigned long now) {
    processSteeringMenu(now, !isWindowMotionBusy());
    processWirelessHorn(now);
    processWindowControl(now);
}

bool isWindowMotionBusy() {
    return !windowGestures.empty();
}
