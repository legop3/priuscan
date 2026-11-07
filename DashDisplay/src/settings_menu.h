#pragma once
#include <stdint.h>
namespace dash_menu {
void begin(void (*writeScreen)(uint8_t), void (*writeStrip)(uint8_t));
void receive(uint8_t state, int8_t adjustment, uint32_t now);
bool stripPreview();
void tick(uint32_t now, bool dim, bool off);
}
