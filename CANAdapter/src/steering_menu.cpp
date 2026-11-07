#include "steering_menu.h"
#include "steering_controls.h"
#include "../../shared/dash_menu_protocol.h"
#include "../../shared/dash_settings.h"
namespace {
using namespace dash_menu_wire;
uint8_t button=STEER_NONE, state=0;
bool selectHeld=false, selectConsumed=false, waitRelease=false;
uint32_t selectAt=0, activityAt=0, heartbeatAt=0, repeatAt=0;
struct Event { uint8_t state; int8_t delta; };
Event queue[32]{};
unsigned head=0, count=0;
void publish(int8_t delta=0) {
    // At most one event per press/repeat; the UART is drained every loop.
    if (count==32) { head=0; count=0; }
    queue[(head+count)%32]={state,delta};
    ++count;
}
void closeMenu() {
    state=0;
    selectConsumed=true;
    waitRelease=button!=STEER_NONE;
    publish();
}
void direction(uint8_t code) {
    if (state & EDITING) publish(code==STEER_UP ? 1 : -1);
    else {
        const unsigned item=selected(state);
        const unsigned next=code==STEER_UP ?
            (item+dash_settings::COUNT-1)%dash_settings::COUNT : (item+1)%dash_settings::COUNT;
        state=withSelection(state,next); publish();
    }
}
}
bool handleSteeringMenu(uint8_t code, uint32_t now) {
    if (code==button) return (state & OPEN) || waitRelease || code==STEER_ENTER;
    const uint8_t previous=button;
    button=code;
    if (waitRelease) {
        if (code==STEER_NONE) waitRelease=false;
        return true;
    }
    if (previous==STEER_ENTER) {
        selectHeld=false;
        // Select acts on release so a hold never also selects an item.
        if ((state & OPEN) && !selectConsumed && code==STEER_NONE) {
            state^=EDITING;
            activityAt=now;
            publish();
        }
    }
    if (code==STEER_ENTER) {
        selectHeld=true; selectConsumed=false; selectAt=now;
        if (state & OPEN) activityAt=now;
        return true;
    }
    if (!(state & OPEN)) return false;
    if (code==STEER_UP || code==STEER_DOWN) {
        activityAt=now; repeatAt=now+450; direction(code);
    } else if (code==STEER_BACK) {
        activityAt=now;
        if (state & EDITING) { state &= ~EDITING; publish(); }
        else closeMenu();
    }
    return true;
}
void processSteeringMenu(uint32_t now, bool windowsIdle) {
    if (selectHeld && !selectConsumed && now-selectAt>=HOLD_MS && windowsIdle) {
        selectConsumed=true;
        if (state & OPEN) closeMenu();
        else { state=OPEN; activityAt=now; publish(); }
    }
    if (state & OPEN) {
        if (now-activityAt>=IDLE_MS) closeMenu();
        else if ((state & EDITING) && (button==STEER_UP || button==STEER_DOWN) &&
                 static_cast<int32_t>(now-repeatAt)>=0) {
            activityAt=now; repeatAt=now+150; direction(button);
        }
    }
    // Periodic authoritative state repairs a lost open/close frame as well.
    if (now-heartbeatAt>=HEARTBEAT_MS) { heartbeatAt=now; publish(); }
    if (count && sendDashMenu(queue[head].state,queue[head].delta)) {
        head=(head+1)%32; --count;
    }
}
