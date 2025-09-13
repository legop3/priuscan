/*
  LVGL v8.x + LovyanGFX v1 + EEZ (UI-only) — optimized
  - One WS2812 show() per frame via dirty flag
  - LVGL widgets update only on change; styles only on band transitions
  - Larger DMA draw buffers if RAM allows
  - No %f: fixed-point helpers for labels (kW, temps)
*/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <limits.h>

#define SHIFT_LED_PIN   38
#define SHIFT_LED_COUNT 8
Adafruit_NeoPixel shiftStrip(SHIFT_LED_COUNT, SHIFT_LED_PIN, NEO_GRB + NEO_KHZ800);

// ============ LED "dirty" batching (one show per frame) ============
static volatile bool g_led_dirty = false;
static inline void led_mark_dirty() { g_led_dirty = true; }
static inline void led_maybe_show() { if (g_led_dirty) { shiftStrip.show(); g_led_dirty = false; } }

// #define EEZ_FOR_LVGL
#define EEZ_UI_MAIN_SYMBOL SCREEN_ID_MAIN

#include <lvgl.h>

// EEZ C headers (C linkage)
#ifdef __cplusplus
extern "C" {
#endif
#include "ui.h"
#include "screens.h"
#ifdef __cplusplus
}
#endif

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

#include "driver/gpio.h"
#include "esp_heap_caps.h"

// ──────────────────────────────────────────────────────────────
// TUNABLES
// ──────────────────────────────────────────────────────────────
#define PCLK_HZ            15000000
#define PCLK_ACTIVE_NEG    0
#define GPIO_DRIVE_LEVEL   GPIO_DRIVE_CAP_1  // 0..3

// ============ UART link: payload & pins (ESP32-S3) ============
#include <HardwareSerial.h>
HardwareSerial& LINK = Serial2;

static const int LINK_RX = 19;
static const int LINK_TX = 20;
static const uint32_t LINK_BAUD = 230400;

#pragma pack(push,1)
struct PayloadF {
  uint8_t seq;
  float   rpm;
  float   hv_current_A;
  float   hv_voltage_V;
  float   ect_C;
  float   hv_intake_C;
  float   tb1_C;
  float   tb2_C;
  float   tb3_C;
  float   soc_pct;
  int8_t  ebar;
  uint8_t est;
  uint8_t bfs;
  uint8_t bfor;
  uint8_t dim;
};
#pragma pack(pop)

// ============ Framed parser (0xAA | LEN | payload | XOR) ============
static inline uint8_t xor_checksum(const uint8_t* p, size_t n) {
  uint8_t x = 0; for (size_t i=0;i<n;++i) x ^= p[i]; return x;
}

enum class RxState : uint8_t { WAIT_START, WAIT_LEN, WAIT_PAYLOAD, WAIT_CSUM };
static RxState  rxState = RxState::WAIT_START;
static uint8_t  rxLen   = 0;
static uint8_t  rxBuf[128];
static uint8_t  rxIdx   = 0;

static const uint8_t START_BYTE = 0xAA;
static const uint8_t EXPECTED_LEN = sizeof(PayloadF);

static volatile bool havePacket = false;
static PayloadF lastPacket{};
static uint8_t  lastSeq = 0;
static unsigned long lastRxMs = 0;

static void pollUart() {
  while (LINK.available()) {
    uint8_t b = (uint8_t)LINK.read();
    switch (rxState) {
      case RxState::WAIT_START:
        if (b == START_BYTE) rxState = RxState::WAIT_LEN;
        break;
      case RxState::WAIT_LEN:
        rxLen = b;
        if (rxLen == 0 || rxLen > sizeof(rxBuf)) { rxState = RxState::WAIT_START; }
        else { rxIdx = 0; rxState = RxState::WAIT_PAYLOAD; }
        break;
      case RxState::WAIT_PAYLOAD:
        rxBuf[rxIdx++] = b;
        if (rxIdx >= rxLen) rxState = RxState::WAIT_CSUM;
        break;
      case RxState::WAIT_CSUM: {
        uint8_t calc = xor_checksum(rxBuf, rxLen);
        if (calc == b && rxLen == EXPECTED_LEN) {
          memcpy(&lastPacket, rxBuf, EXPECTED_LEN);
          havePacket = true;
          lastSeq = lastPacket.seq;
          lastRxMs = millis();
        }
        rxState = RxState::WAIT_START;
        break;
      }
    }
  }
}

// ──────────────────────────────────────────────────────────────
// Drive strength on RGB/sync/PCLK pins
// ──────────────────────────────────────────────────────────────
static void set_rgb_drive_strength() {
  const gpio_num_t pins[] = {
    // B0..B4
    GPIO_NUM_15, GPIO_NUM_7, GPIO_NUM_6, GPIO_NUM_5, GPIO_NUM_4,
    // G0..G5
    GPIO_NUM_9,  GPIO_NUM_46, GPIO_NUM_3, GPIO_NUM_8, GPIO_NUM_16, GPIO_NUM_1,
    // R0..R4
    GPIO_NUM_14, GPIO_NUM_21, GPIO_NUM_47, GPIO_NUM_48, GPIO_NUM_45,
    // DE, VS, HS, PCLK
    GPIO_NUM_41, GPIO_NUM_40, GPIO_NUM_39, GPIO_NUM_0
  };
  for (auto p : pins) { gpio_set_drive_capability(p, GPIO_DRIVE_LEVEL); }
}

// ──────────────────────────────────────────────────────────────
// LovyanGFX device
// ──────────────────────────────────────────────────────────────
class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB     _bus_instance;
  lgfx::Panel_RGB   _panel_instance;

  LGFX(void) {
    { // Bus config
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;

      // RGB data pins
      cfg.pin_d0  = GPIO_NUM_15; // B0
      cfg.pin_d1  = GPIO_NUM_7;  // B1
      cfg.pin_d2  = GPIO_NUM_6;  // B2
      cfg.pin_d3  = GPIO_NUM_5;  // B3
      cfg.pin_d4  = GPIO_NUM_4;  // B4

      cfg.pin_d5  = GPIO_NUM_9;  // G0
      cfg.pin_d6  = GPIO_NUM_46; // G1
      cfg.pin_d7  = GPIO_NUM_3;  // G2
      cfg.pin_d8  = GPIO_NUM_8;  // G3
      cfg.pin_d9  = GPIO_NUM_16; // G4
      cfg.pin_d10 = GPIO_NUM_1;  // G5

      cfg.pin_d11 = GPIO_NUM_14; // R0
      cfg.pin_d12 = GPIO_NUM_21; // R1
      cfg.pin_d13 = GPIO_NUM_47; // R2
      cfg.pin_d14 = GPIO_NUM_48; // R3
      cfg.pin_d15 = GPIO_NUM_45; // R4

      // Sync pins
      cfg.pin_henable = GPIO_NUM_41;
      cfg.pin_vsync   = GPIO_NUM_40;
      cfg.pin_hsync   = GPIO_NUM_39;
      cfg.pin_pclk    = GPIO_NUM_0;

      cfg.freq_write      = PCLK_HZ;
      cfg.pclk_active_neg = PCLK_ACTIVE_NEG;
      cfg.de_idle_high    = 0;
      cfg.pclk_idle_high  = 0;

      // timings
      cfg.hsync_polarity    = 1;
      cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48;
      cfg.hsync_back_porch  = 40;

      cfg.vsync_polarity    = 1;
      cfg.vsync_front_porch = 13;
      cfg.vsync_pulse_width = 1;
      cfg.vsync_back_porch  = 31;

      _bus_instance.config(cfg);
    }

    { // Panel config
      auto cfg = _panel_instance.config();
      cfg.memory_width  = 800;
      cfg.memory_height = 480;
      cfg.panel_width   = 800;
      cfg.panel_height  = 480;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      _panel_instance.config(cfg);
    }

    _panel_instance.setBus(&_bus_instance);
    setPanel(&_panel_instance);
  }
};

LGFX lcd;

// ──────────────────────────────────────────────────────────────
// LVGL display driver (v8.x)
// ──────────────────────────────────────────────────────────────
static uint32_t screenWidth;
static uint32_t screenHeight;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;
static lv_disp_drv_t disp_drv;

// Try bigger first for fewer flushes:
static const int TARGET_BUF_LINES[] = { 160, 120, 96, 80, 64, 48, 32, 24, 16 };

static bool alloc_lvgl_draw_buffers(uint16_t w) {
  for (int lines : TARGET_BUF_LINES) {
    size_t px    = (size_t)w * (size_t)lines;
    size_t bytes = px * sizeof(lv_color_t);
    lv_color_t *b1 = (lv_color_t*)heap_caps_malloc(bytes, MALLOC_CAP_DMA);
    lv_color_t *b2 = (lv_color_t*)heap_caps_malloc(bytes, MALLOC_CAP_DMA);
    if (b1 && b2) {
      buf1 = b1; buf2 = b2;
      lv_disp_draw_buf_init(&draw_buf, buf1, buf2, px);
      Serial.printf("LVGL draw buffers: %d lines x2 (%u bytes each)\n", lines, (unsigned)bytes);
      return true;
    }
    if (b1) heap_caps_free(b1);
    if (b2) heap_caps_free(b2);
  }
  // single-buffer fallback
  int lines = 16;
  size_t px    = (size_t)w * (size_t)lines;
  size_t bytes = px * sizeof(lv_color_t);
  buf1 = (lv_color_t*)heap_caps_malloc(bytes, MALLOC_CAP_DMA);
  if (buf1) {
    lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, px);
    Serial.printf("LVGL draw buffer: %d lines (single) (%u bytes)\n", lines, (unsigned)bytes);
    return true;
  }
  return false;
}

// Safe DMA flush (LovyanGFX v1)
static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  lcd.pushImageDMA(area->x1, area->y1, w, h, (const lgfx::rgb565_t*)&color_p->full);
  lcd.waitDMA();
  lv_disp_flush_ready(disp);
}

// LVGL tick @ true 5ms
static hw_timer_t * lv_tick_timer = nullptr;
static void IRAM_ATTR onLvglTick() { lv_tick_inc(5); }

// ──────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────
inline float c_to_f(float c) { return (c * 9.0f / 5.0f) + 32.0f; }

// Change guards
template<typename T>
static inline bool changed(T &prev, T now) { if (prev == now) return false; prev = now; return true; }

// LED color helper
static inline uint32_t RGB(uint8_t r, uint8_t g, uint8_t b){ return shiftStrip.Color(r,g,b); }

// **Fixed-point label helper (centi-units, i.e., value * 100)**
static inline void label_set_centi(lv_obj_t* lbl, int centi, const char* suffix) {
  int sign = (centi < 0) ? -1 : 1;
  int a = sign * centi;
  int whole = a / 100;
  int frac  = a % 100;
  if (sign < 0) lv_label_set_text_fmt(lbl, "-%d.%02d%s", whole, frac, suffix);
  else          lv_label_set_text_fmt(lbl,  "%d.%02d%s", whole, frac, suffix);
}

// ── tunables for strip ───────────────────────────────────────────────────
#define SHIFT_RPM_ON_THRESH   800   // treat engine "on" when RPM > ~800
#define EBAR_WARN_ON          48    // start warning near engine-on at 50
#define EBAR_WARN_OFF         46    // hysteresis to exit warn
#define BLINK_TOGGLES_MAX      8    // ~4 full blinks per crossing
#define BLINK_PERIOD_NEAR_MS 120
#define BLINK_PERIOD_OVER_MS  90
#define ENGINE_INDICATOR_COLOR  RGB(255,100,0)

// LED strip logic (no .show() inside; uses led_mark_dirty())
void updateShiftStrip(int8_t ebar_raw, float rpm) {
  static bool     wasInWarn = false;
  static uint8_t  blinksRemaining = 0;
  static bool     blinkOn = true;
  static uint32_t lastBlinkMs = 0;

  const int N = SHIFT_LED_COUNT;

  // Engine running → center pair orange
  if (rpm > SHIFT_RPM_ON_THRESH) {
    for (int i=0;i<N;i++) shiftStrip.setPixelColor(i, 0);
    if (N >= 2) {
      int left  = (N / 2) - 1;
      int right =  N / 2;
      shiftStrip.setPixelColor(left,  ENGINE_INDICATOR_COLOR);
      shiftStrip.setPixelColor(right, ENGINE_INDICATOR_COLOR);
    } else if (N == 1) {
      shiftStrip.setPixelColor(0, ENGINE_INDICATOR_COLOR);
    }
    wasInWarn = false; blinksRemaining = 0; blinkOn = true;
    led_mark_dirty();
    return;
  }

  // Regen (blue) right→left
  if (ebar_raw < 0) {
    int mag = -ebar_raw; if (mag > 100) mag = 100;
    int lit = (mag * N) / 100;
    for (int i=0;i<N;i++) shiftStrip.setPixelColor(i, 0);
    for (int k=0; k<lit; ++k) {
      int idx = (N-1) - k;
      shiftStrip.setPixelColor(idx, RGB(0,80,255));
    }
    wasInWarn = false; blinksRemaining = 0; blinkOn = true;
    led_mark_dirty();
    return;
  }

  // Positive accel (warn near 48)
  int ebar_pos = ebar_raw > 0 ? ebar_raw : 0; if (ebar_pos > 100) ebar_pos = 100;
  bool inWarnNow = (!wasInWarn) ? (ebar_pos >= EBAR_WARN_ON)
                                : (ebar_pos >  EBAR_WARN_OFF);

  if (inWarnNow && !wasInWarn) {
    blinksRemaining = BLINK_TOGGLES_MAX;
    blinkOn = true;
    lastBlinkMs = millis();
  }
  wasInWarn = inWarnNow;

  if (inWarnNow) {
    uint32_t period = (ebar_pos >= 50) ? BLINK_PERIOD_OVER_MS : BLINK_PERIOD_NEAR_MS;
    if (blinksRemaining > 0) {
      uint32_t now = millis();
      if (now - lastBlinkMs >= period) { lastBlinkMs = now; blinkOn = !blinkOn; blinksRemaining--; }
      uint32_t c = blinkOn ? RGB(255,0,0) : RGB(40,0,0);
      for (int i=0;i<N;i++) shiftStrip.setPixelColor(i, c);
    } else {
      for (int i=0;i<N;i++) shiftStrip.setPixelColor(i, RGB(255,0,0));
    }
    led_mark_dirty();
    return;
  }

  // Below warn: progressive fill
  int lit = (ebar_pos * N) / EBAR_WARN_ON; if (lit < 0) lit = 0; if (lit > N) lit = N;
  for (int i=0;i<N;i++) {
    if (i < lit) {
      uint32_t c =
        (i < N*3/8) ? RGB(0,255,0) :
        (i < N*5/8) ? RGB(180,180,0) :
        (i < N*7/8) ? RGB(255,80,0) :
                      RGB(255,0,0);
      shiftStrip.setPixelColor(i, c);
    } else {
      shiftStrip.setPixelColor(i, 0);
    }
  }
  led_mark_dirty();
}

// ──────────────────────────────────────────────────────────────
// Brightness
// ──────────────────────────────────────────────────────────────
uint8_t ui_brightness_pct = 60;

void setShiftStripBrightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  ui_brightness_pct = pct;
  uint8_t neo = map(pct, 0, 100, 0, 255);
  shiftStrip.setBrightness(neo); // applies on next show()
}

#define BL_PIN        2
#define BL_CH         0
#define BL_FREQ       20000
#define BL_RES_BITS   10

void setBacklightPct(uint8_t pct) {
  if (pct > 100) pct = 100;
  uint32_t duty = (uint32_t)pct * ((1 << BL_RES_BITS) - 1) / 100;
  ledcWrite(BL_CH, duty);
}

void backlightInit() {
  ledcSetup(BL_CH, BL_FREQ, BL_RES_BITS);
  ledcAttachPin(BL_PIN, BL_CH);
  setBacklightPct(10);
}

// ──────────────────────────────────────────────────────────────
// SETUP / LOOP
// ──────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  LINK.begin(LINK_BAUD, SERIAL_8N1, LINK_RX, LINK_TX);
  Serial.printf("UART link on Serial2 @ %lu, RX=%d TX=%d\n",
                (unsigned long)LINK_BAUD, LINK_RX, LINK_TX);

  set_rgb_drive_strength();

  lcd.begin();
  backlightInit();

  lv_init();

  screenWidth  = lcd.width();
  screenHeight = lcd.height();
  Serial.printf("Screen: %ux%u\n", (unsigned)screenWidth, (unsigned)screenHeight);

  if (!alloc_lvgl_draw_buffers(screenWidth)) {
    Serial.println("FATAL: could not allocate LVGL DMA buffers");
    for(;;) delay(1000);
  }

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = screenWidth;
  disp_drv.ver_res  = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

  // tighten internal refresh timer to ~10 ms
  lv_timer_t *refr = _lv_disp_get_refr_timer(disp);
  if (refr) lv_timer_set_period(refr, 10);

  // 5ms LVGL tick
  lv_tick_timer = timerBegin(0, 80, true); // 80 MHz / 80 = 1 MHz
  timerAttachInterrupt(lv_tick_timer, &onLvglTick, true);
  timerAlarmWrite(lv_tick_timer, 5000, true);
  timerAlarmEnable(lv_tick_timer);

  ui_init();

  shiftStrip.begin();
  shiftStrip.setBrightness(255);
  shiftStrip.clear();
  shiftStrip.show();
}

// define colors ahead of time
lv_color_t g_green  = lv_color_hex(0x00ff26);
lv_color_t g_blue   = lv_color_hex(0x00ffff);
lv_color_t g_orange = lv_color_hex(0xFCA200);
lv_color_t g_red    = lv_color_hex(0xFD0000);
lv_color_t g_yellow = lv_color_hex(0xE9E800);

// prev-value cache for change guards
// static bool     prev_data_state = false;

static int      prev_rpm = INT_MIN;
static int      prev_watts = INT_MIN;
static int      prev_kw_start = INT_MIN;
static int      prev_kw_value = INT_MIN;
static uint8_t  prev_kw_sign = 255;

static int      prev_soc = INT_MIN;
static uint8_t  prev_soc_band = 255;

static int      prev_btF = INT_MIN;        // rounded F° for label
static uint8_t  prev_bt_band = 255;

static int      prev_intakeF = INT_MIN;
static uint8_t  prev_intake_band = 255;

static int      prev_bfs = INT_MIN;
static uint8_t  prev_bfor = 255;

static int      prev_bt1c = INT_MIN;       // centi-F (x100)
static int      prev_bt2c = INT_MIN;
static int      prev_bt3c = INT_MIN;

static int      prev_batt_v = INT_MIN;
static int      prev_batt_a = INT_MIN;

static uint8_t  prev_dim = 255;

static int      prev_ebar = INT_MIN;
static int      prev_est  = INT_MIN;

void loop() {
  lv_timer_handler();
  pollUart();

  // UI update cadence (every ~50 ms)
  static unsigned long lastUi = 0;
  unsigned long now = millis();
  if (now - lastUi >= 50) {
    lastUi = now;

    bool fresh = (now - lastRxMs) < 500;
    if (fresh) {
      lv_obj_add_flag(objects.no_data_label, LV_OBJ_FLAG_HIDDEN);

      // ===== RPM =====
      int rpm_val = (int)lrintf(lastPacket.rpm);
      rpm_val = constrain(rpm_val, 0, 100000);
      if (changed(prev_rpm, rpm_val)) {
        lv_bar_set_value(objects.rpm_bar, rpm_val, LV_ANIM_OFF);
        lv_label_set_text_fmt(objects.rpm_label, "%d\nRPM", rpm_val);
      }

      // ===== Watts bar & label =====
      int watts = (int)lrintf(lastPacket.hv_voltage_V * lastPacket.hv_current_A);
      uint8_t sign = (watts >= 0) ? 1 : 0;

      if (changed(prev_kw_sign, sign)) {
        if (sign) {
          lv_obj_set_style_bg_color(objects.kw_watts_bar, g_green, LV_PART_INDICATOR);
          lv_obj_set_style_opa(objects.kw_watts_bar, LV_OPA_COVER, LV_PART_INDICATOR);
        } else {
          lv_obj_set_style_bg_color(objects.kw_watts_bar, g_blue, LV_PART_INDICATOR);
          lv_obj_set_style_opa(objects.kw_watts_bar, LV_OPA_COVER, LV_PART_INDICATOR);
        }
      }

      if (changed(prev_watts, watts)) {
        if (sign) {
          int start = -20000;
          int val   = watts - 20000;
          if (changed(prev_kw_start, start)) lv_bar_set_start_value(objects.kw_watts_bar, start, LV_ANIM_OFF);
          if (changed(prev_kw_value, val))   lv_bar_set_value(objects.kw_watts_bar, val, LV_ANIM_OFF);
        } else {
          int start = watts + 20000;
          int val   = 20000;
          if (changed(prev_kw_start, start)) lv_bar_set_start_value(objects.kw_watts_bar, start, LV_ANIM_OFF);
          if (changed(prev_kw_value, val))   lv_bar_set_value(objects.kw_watts_bar, val, LV_ANIM_OFF);
        }

        // kW label (centi-kW = W/10)
        int kw_centi = (int)lrintf(watts / 10.0f);
        label_set_centi(objects.kw_label, kw_centi, "\nkW");
      }

      // ===== Battery SoC panel =====
      int soc_r = (int)lrintf(lastPacket.soc_pct);
      if (changed(prev_soc, soc_r)) {
        lv_label_set_text_fmt(objects.battery_soc, "%d%%\nSoC", soc_r);
      }
      uint8_t soc_band =
        (soc_r < 45) ? 0 :
        (soc_r < 50) ? 1 :
        (soc_r < 60) ? 2 : 3;
      if (changed(prev_soc_band, soc_band)) {
        lv_color_t c = (soc_band==0)?g_red:(soc_band==1)?g_orange:(soc_band==2)?g_yellow:g_blue;
        lv_obj_set_style_bg_color(objects.battery_info_panel, c, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_info_panel, LV_OPA_COVER, LV_PART_MAIN);
      }

      // ===== Battery temp (avg) integer label + banded color =====
      float battery_temp_avg = (lastPacket.tb1_C + lastPacket.tb2_C + lastPacket.tb3_C) / 3.0f;
      int btF_round = (int)lrintf(c_to_f(battery_temp_avg));
      if (changed(prev_btF, btF_round)) {
        lv_label_set_text_fmt(objects.battery_temp, "%d°", btF_round);
      }
      uint8_t bt_band =
        (btF_round < 60) ? 0 :
        (btF_round < 80) ? 1 :
        (btF_round < 90) ? 2 :
        (btF_round < 100)? 3 : 4;
      if (changed(prev_bt_band, bt_band)) {
        lv_color_t c = (bt_band==0)?g_blue:(bt_band==1)?g_green:(bt_band==2)?g_yellow:(bt_band==3)?g_orange:g_red;
        lv_obj_set_style_bg_color(objects.battery_temp, c, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_temp, LV_OPA_COVER, LV_PART_MAIN);
      }

      // ===== Intake temp integer label + banded color =====
      int intakeF_round = (int)lrintf(c_to_f(lastPacket.hv_intake_C));
      static int prev_intake_label = INT_MIN;
      if (changed(prev_intake_label, intakeF_round)) {
        lv_label_set_text_fmt(objects.battery_intake_temp, "%d°\nIntake", intakeF_round);
      }
      uint8_t intake_band =
        (intakeF_round < 60) ? 0 :
        (intakeF_round < 80) ? 1 :
        (intakeF_round < 90) ? 2 :
        (intakeF_round < 100)? 3 : 4;
      if (changed(prev_intake_band, intake_band)) {
        lv_color_t c = (intake_band==0)?g_blue:(intake_band==1)?g_green:(intake_band==2)?g_yellow:(intake_band==3)?g_orange:g_red;
        lv_obj_set_style_bg_color(objects.battery_fan_info_panel, c, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_fan_info_panel, LV_OPA_COVER, LV_PART_MAIN);
      }

      // ===== Battery fan info =====
      int bfs = (int)lrintf(lastPacket.bfs);
      if (changed(prev_bfs, bfs)) {
        lv_label_set_text_fmt(objects.battery_fan_speed, "S: %d", bfs);
      }
      uint8_t bfor = lastPacket.bfor ? 1 : 0;
      if (changed(prev_bfor, bfor)) {
        if (bfor) {
          lv_obj_set_style_bg_color(objects.battery_fan_control, g_green, LV_PART_MAIN);
          lv_obj_set_style_opa(objects.battery_fan_control, LV_OPA_COVER, LV_PART_MAIN);
          lv_label_set_text(objects.fan_control_label, "Control\nEnabled");
        } else {
          lv_obj_set_style_bg_color(objects.battery_fan_control, g_red, LV_PART_MAIN);
          lv_obj_set_style_opa(objects.battery_fan_control, LV_OPA_COVER, LV_PART_MAIN);
          lv_label_set_text(objects.fan_control_label, "Control\nDisabled");
        }
      }

      // ===== Three battery temps (two decimals, no %f) =====
      int bt1c = (int)lrintf(c_to_f(lastPacket.tb1_C) * 100.0f);
      int bt2c = (int)lrintf(c_to_f(lastPacket.tb2_C) * 100.0f);
      int bt3c = (int)lrintf(c_to_f(lastPacket.tb3_C) * 100.0f);
      if (changed(prev_bt1c, bt1c)) label_set_centi(objects.bt1, bt1c, "°");
      if (changed(prev_bt2c, bt2c)) label_set_centi(objects.bt2, bt2c, "°");
      if (changed(prev_bt3c, bt3c)) label_set_centi(objects.bt3, bt3c, "°");

      // ===== Battery V/A integer labels =====
      int battery_voltage = (int)lrintf(lastPacket.hv_voltage_V);
      int battery_amperage = (int)lrintf(lastPacket.hv_current_A);
      if (changed(prev_batt_v, battery_voltage)) lv_label_set_text_fmt(objects.battery_voltage, "%dV", battery_voltage);
      if (changed(prev_batt_a, battery_amperage)) lv_label_set_text_fmt(objects.battery_amperage, "%dA", battery_amperage);

      // ===== Shift LED strip =====
      updateShiftStrip(lastPacket.ebar, (float)rpm_val);
      led_maybe_show();  // single WS2812 transfer per frame

      // ===== Dimming (on change) =====
      uint8_t dim_now = lastPacket.dim ? 1 : 0;
      if (changed(prev_dim, dim_now)) {
        if (dim_now) { setBacklightPct(30); setShiftStripBrightness(5); }
        else          { setBacklightPct(100); setShiftStripBrightness(50); }
      }

      // ===== Ebar & drain labels (on change) =====
      int ebar_round = (int)lrintf(lastPacket.ebar);
      if (changed(prev_ebar, ebar_round)) {
        lv_label_set_text_fmt(objects.ebar_label, "Ebar: %d", ebar_round);
        // update ebar bar
        lv_bar_set_value(objects.ebar_bar, ebar_round, LV_ANIM_OFF);
      };
\
      int drain_round = (int)lrintf(lastPacket.est);
      if (changed(prev_est, drain_round)) lv_label_set_text_fmt(objects.energy_drain, "Mode: %d", drain_round);



    } else {
      // stale: optional handling (no-op)
      lv_obj_clear_flag(objects.no_data_label, LV_OBJ_FLAG_HIDDEN);
    }
  }
}
