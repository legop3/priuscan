#ifndef DASH_SETTINGS_H
#define DASH_SETTINGS_H
#include <stdint.h>
namespace dash_settings {
enum class Output : uint8_t { Screen, Strip };
struct Setting {
    const char* label;
    const char* key; // Stable NVS key: independent of menu position.
    Output output;
    bool night;
    uint8_t minimum, maximum, step, defaultValue;
};
// Append entries here to extend the same menu; the UART frame stays unchanged.
constexpr Setting ITEMS[]={
    {"Screen / Day",   "screen-day",   Output::Screen,false,1,100,1,100},
    {"Screen / Night", "screen-night", Output::Screen,true, 1,100,1,30},
    {"LED strip / Day",   "strip-day",   Output::Strip,false,0,100,1,50},
    {"LED strip / Night", "strip-night", Output::Strip,true, 0,100,1,5},
};
constexpr unsigned COUNT=sizeof(ITEMS)/sizeof(ITEMS[0]);
static_assert(COUNT>0 && COUNT<=64,"Menu selection must fit its six-bit item index");
}
#endif
