#ifndef DASH_MENU_PROTOCOL_H
#define DASH_MENU_PROTOCOL_H
#include <stdint.h>
namespace dash_menu_wire {
// Separate UART frame: marker, length, state, signed adjustment, XOR.
// Existing sensor and matrix packets are unchanged.
constexpr uint8_t MARKER=0xAB, PAYLOAD_SIZE=2;
constexpr uint8_t OPEN=1, EDITING=2;
constexpr unsigned ITEM_SHIFT=2;
inline unsigned selected(uint8_t state) { return state>>ITEM_SHIFT; }
inline uint8_t withSelection(uint8_t state, unsigned item) {
    return (state & (OPEN|EDITING)) | static_cast<uint8_t>(item<<ITEM_SHIFT);
}
constexpr uint32_t HOLD_MS=1000, IDLE_MS=15000, HEARTBEAT_MS=500, LINK_TIMEOUT_MS=2000;
}
#endif
