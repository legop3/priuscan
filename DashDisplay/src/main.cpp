/*
  LVGL v8.x + LovyanGFX v1 + EEZ (UI-only) — high-FPS, artifact-hardened
  - PCLK increased (default 24 MHz; adjust below)
  - Toggleable PCLK sampling edge (try 0/1 if you see horizontal smear)
  - Stronger GPIO drive on RGB/sync/PCLK pins (ESP32-S3)
  - DMA-capable double draw buffers in internal SRAM
  - Safe DMA flush: pushImageDMA + waitDMA() before lv_disp_flush_ready
  - True 5 ms LVGL tick; refresh timer set to 10 ms (v8: via _lv_disp_get_refr_timer)
*/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

// #define EEZ_FOR_LVGL
#define EEZ_UI_MAIN_SYMBOL SCREEN_ID_MAIN

#include <lvgl.h>

// EEZ C headers (C linkage)
#ifdef __cplusplus
extern "C" {
#endif
#include "ui.h"
#include "screens.h"
// #include "styles.h"
#ifdef __cplusplus
}
#endif

#define GFX_BL  2   // Backlight pin

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// ESP32-S3 helpers for drive strength + DMA allocations
#include "driver/gpio.h"
#include "esp_heap_caps.h"

// ──────────────────────────────────────────────────────────────
// TUNABLES
// ──────────────────────────────────────────────────────────────
// #define PCLK_HZ            10000000   // ↑ try 24 MHz first; raise only if clean
#define PCLK_HZ            15000000   // ↑ try 24 MHz first; raise only if clean
#define PCLK_ACTIVE_NEG    0          // try 0 or 1; wrong edge => horizontal scrambling
#define GPIO_DRIVE_LEVEL   GPIO_DRIVE_CAP_1  // 0..3 (S3); 3 = strongest

// ============ UART link: payload & pins (ESP32‑S3) ============
#include <HardwareSerial.h>
HardwareSerial& LINK = Serial2;

// Use the pins you mentioned:
static const int LINK_RX = 19;     // from sender's TX
static const int LINK_TX = 20;     // to sender's RX (usually unused on display)
static const uint32_t LINK_BAUD = 230400;  // match the sender

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
};
#pragma pack(pop)

// ============ Simple framed parser (0xAA | LEN | payload | XOR) ============
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
// Apply stronger drive on all RGB pins + sync + PCLK
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
// LovyanGFX display for ESP32-S3 RGB panel (800x480)
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
      cfg.pclk_active_neg = PCLK_ACTIVE_NEG; // sampling edge
      cfg.de_idle_high    = 0;
      cfg.pclk_idle_high  = 0;

      // Keep your proven baseline timings at higher PCLK (tighten later if clean)
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

// Allocate two DMA-capable line buffers in internal SRAM (fallback ladder)
static const int TARGET_BUF_LINES[] = { 80, 64, 48, 40, 32, 24, 16 };

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
  // single-buffer fallback (still DMA RAM)
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

// Safe DMA flush (LovyanGFX v1): waitDMA() before releasing the buffer
static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  lcd.pushImageDMA(area->x1, area->y1, w, h, (const lgfx::rgb565_t*)&color_p->full);
  lcd.waitDMA();                 // <— correct v1 API (was dmaWait)
  lv_disp_flush_ready(disp);
}

// ──────────────────────────────────────────────────────────────
// LVGL tick @ real 5ms (v8.x)
// ──────────────────────────────────────────────────────────────
static hw_timer_t * lv_tick_timer = nullptr;
static void IRAM_ATTR onLvglTick() {
  lv_tick_inc(5);  // true 5 ms
}

// ──────────────────────────────────────────────────────────────
// Helpers (optional)
// ──────────────────────────────────────────────────────────────
static void inspect_active_screen() {
  lv_obj_t *scr = lv_scr_act();
  uint32_t cnt = lv_obj_get_child_cnt(scr);
  Serial.printf("EEZ/LVGL: active screen children = %u\n", (unsigned)cnt);
  for (uint32_t i = 0; i < cnt; i++) {
    lv_obj_t *c = lv_obj_get_child(scr, i);
    Serial.printf("  [%u] hidden=%d opa=%u w=%d h=%d\n", i,
      lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN),
      (unsigned)lv_obj_get_style_opa(c, 0),
      (int)lv_obj_get_width(c),
      (int)lv_obj_get_height(c));
  }
}

inline float c_to_f(float c) {
    return (c * 9.0f / 5.0f) + 32.0f;
}


// ──────────────────────────────────────────────────────────────
// SETUP / LOOP
// ──────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

    // ---- UART link init (display side) ----
  LINK.begin(LINK_BAUD, SERIAL_8N1, LINK_RX, LINK_TX);
  Serial.printf("UART link on Serial2 @ %lu, RX=%d TX=%d\n",
                (unsigned long)LINK_BAUD, LINK_RX, LINK_TX);


  Serial.println("Start: LVGL + EEZ (UI-only) — high-FPS SI-hardened (LVGL v8)");

  // Strengthen drive BEFORE lcd.begin()
  set_rgb_drive_strength();

  lcd.setRotation(2);
  lcd.begin();
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
  // lcd.fillRect(0, 0, 240, 240, 0x07E0 /* pure green in RGB565 */);
  // delay(300);
  // lcd.clear();

  // delay(1000);
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

  // v8.x: tighten the internal refresh timer to ~10 ms
  lv_timer_t *refr = _lv_disp_get_refr_timer(disp);   // internal getter is available in v8
  if (refr) lv_timer_set_period(refr, 10);

  // 5ms LVGL tick (hardware timer)
  lv_tick_timer = timerBegin(0, 80, true); // 80 MHz / 80 = 1 MHz
  timerAttachInterrupt(lv_tick_timer, &onLvglTick, true);
  timerAlarmWrite(lv_tick_timer, 5000, true); // 5000 us = 5 ms
  timerAlarmEnable(lv_tick_timer);

  delay(200);
  ui_init();
  // inspect_active_screen();

  // Print expected FPS with current totals (928 x 525)
  // const uint32_t Htot = 800 + 40 + 48 + 40;
  // const uint32_t Vtot = 480 + 1  + 31 + 13;
  // float fps = (float)PCLK_HZ / (float)(Htot * Vtot);
  // Serial.printf("Timing: PCLK=%lu Hz, Htot=%lu, Vtot=%lu -> FPS≈%.2f\n",
  //               (unsigned long)PCLK_HZ, (unsigned long)Htot, (unsigned long)Vtot, fps);


}

// define colors ahead of time, for consistency
lv_color_t g_green = lv_color_hex(0x00ff26);
lv_color_t g_blue = lv_color_hex(0x00ffff);
lv_color_t g_orange = lv_color_hex(0xffa000);
lv_color_t g_red = lv_color_hex(0xFF2B2B);
lv_color_t g_yellow = lv_color_hex(0xE9E800);


void loop() {
  lv_timer_handler();

  // ---- pump UART ----
  pollUart();

  // ---- UI update cadence (every ~50 ms) ----
  static unsigned long lastUi = 0;
  unsigned long now = millis();
  if (now - lastUi >= 50) {
    lastUi = now;

    // If data is fresh (< 500 ms since last packet), show it; otherwise dim/fallback
    bool fresh = (now - lastRxMs) < 500;

    if (fresh) {


      // RPM bar
      int rpm_val = (int)roundf(lastPacket.rpm);
      rpm_val = constrain(rpm_val, 0, 100000); // guard
      lv_bar_set_value(objects.rpm_bar, rpm_val, LV_ANIM_OFF);
      lv_label_set_text_fmt(objects.rpm_label, "%d\nRPM", rpm_val);


      // Watts bar
      int watts = round((lastPacket.hv_voltage_V * lastPacket.hv_current_A));
      // change direction of meter based on watts in (negative) or watts out (positive)
      if(watts >= 0) {
        // if watts out start bar from bottom, change color to green
        lv_bar_set_start_value(objects.kw_watts_bar, -20000, LV_ANIM_OFF);
        lv_bar_set_value(objects.kw_watts_bar, (watts - 20000), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(objects.kw_watts_bar, lv_color_hex(0x00ff26), LV_PART_INDICATOR);
        lv_obj_set_style_opa(objects.kw_watts_bar, LV_OPA_COVER, LV_PART_INDICATOR);
      } else {
        // if watts in start bar from top, change color to blue
        lv_bar_set_value(objects.kw_watts_bar, 20000, LV_ANIM_OFF);
        lv_bar_set_start_value(objects.kw_watts_bar, (watts + 20000), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(objects.kw_watts_bar, lv_color_hex(0x00ffff), LV_PART_INDICATOR);
        lv_obj_set_style_opa(objects.kw_watts_bar, LV_OPA_COVER, LV_PART_INDICATOR);
      }
      // label it in kW though
      float kW = (watts / 1000.0f);
      char kw_buf[16];
      snprintf(kw_buf, sizeof(kw_buf), "%.2f\nkW", kW);
      lv_label_set_text(objects.kw_label, kw_buf);


      // battery soc and temp panel
      int battery_soc_rnd = (int)roundf(lastPacket.soc_pct);
      // Serial.println("battery soc" + String(battery_soc));
      lv_label_set_text_fmt(objects.battery_soc, "%d%%\nSoC", battery_soc_rnd);
      // set backround of objects.battery_info_panel based on soc:
      if (battery_soc_rnd < 45) {
        lv_obj_set_style_bg_color(objects.battery_info_panel, g_red, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_info_panel, LV_OPA_COVER, LV_PART_MAIN);
      } else if (battery_soc_rnd < 50) {
        lv_obj_set_style_bg_color(objects.battery_info_panel, g_orange, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_info_panel, LV_OPA_COVER, LV_PART_MAIN);
      } else if (battery_soc_rnd < 60) {
        lv_obj_set_style_bg_color(objects.battery_info_panel, g_yellow, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_info_panel, LV_OPA_COVER, LV_PART_MAIN);
      } else {
        lv_obj_set_style_bg_color(objects.battery_info_panel, g_blue, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_info_panel, LV_OPA_COVER, LV_PART_MAIN);
      }


      // temp
      // average battery temps into one number
      float battery_temp_avg = (lastPacket.tb1_C + lastPacket.tb2_C + lastPacket.tb3_C) / 3.0f;
      // Serial.println("battery temp avg: " + String(battery_temp_avg, 2));
      float battery_temp_f = c_to_f(battery_temp_avg);
      int battery_temp_f_round = (int)roundf(battery_temp_f);
      lv_label_set_text_fmt(objects.battery_temp, "%d°", battery_temp_f_round);
      // set color based on temp in F:
      if (battery_temp_f < 60) {
        lv_obj_set_style_bg_color(objects.battery_temp, g_blue, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_temp, LV_OPA_COVER, LV_PART_MAIN);
      } else if (battery_temp_f < 80) {
        lv_obj_set_style_bg_color(objects.battery_temp, g_green, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_temp, LV_OPA_COVER, LV_PART_MAIN);
      } else if (battery_temp_f < 90) {
        lv_obj_set_style_bg_color(objects.battery_temp, g_yellow, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_temp, LV_OPA_COVER, LV_PART_MAIN);
      } else if (battery_temp_f < 100) {
        lv_obj_set_style_bg_color(objects.battery_temp, g_orange, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_temp, LV_OPA_COVER, LV_PART_MAIN);
      } else {
        lv_obj_set_style_bg_color(objects.battery_temp, g_red, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_temp, LV_OPA_COVER, LV_PART_MAIN);
      }

      // battery fan info panel
      float intake_temp_f = c_to_f(lastPacket.hv_intake_C);
      int intake_temp_f_round = (int)roundf(intake_temp_f);
      lv_label_set_text_fmt(objects.battery_intake_temp, "%d°", intake_temp_f_round);
      // set color of panel based on intake temp in F:
      if (intake_temp_f < 60) {
        lv_obj_set_style_bg_color(objects.battery_fan_info_panel, g_blue, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_fan_info_panel, LV_OPA_COVER, LV_PART_MAIN);
      } else if (intake_temp_f < 80) {
        lv_obj_set_style_bg_color(objects.battery_fan_info_panel, g_green, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_fan_info_panel, LV_OPA_COVER, LV_PART_MAIN);
      } else if (intake_temp_f < 90) {
        lv_obj_set_style_bg_color(objects.battery_fan_info_panel, g_yellow, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_fan_info_panel, LV_OPA_COVER, LV_PART_MAIN);
      } else if (intake_temp_f < 100) {
        lv_obj_set_style_bg_color(objects.battery_fan_info_panel, g_orange, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_fan_info_panel, LV_OPA_COVER, LV_PART_MAIN);
      } else {
        lv_obj_set_style_bg_color(objects.battery_fan_info_panel, g_red, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_fan_info_panel, LV_OPA_COVER, LV_PART_MAIN);
      }
      

      // change battery fan speed and control LED
      int bfs = (int)roundf(lastPacket.bfs);
      // lv_label_set_text(objects.battery_fan_speed, bfs);
      lv_label_set_text_fmt(objects.battery_fan_speed, "%d", bfs);
      if (lastPacket.bfor) {
        lv_obj_set_style_bg_color(objects.battery_fan_control, g_green, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_fan_control, LV_OPA_COVER, LV_PART_MAIN);
      } else {
        lv_obj_set_style_bg_color(objects.battery_fan_control, g_red, LV_PART_MAIN);
        lv_obj_set_style_opa(objects.battery_fan_control, LV_OPA_COVER, LV_PART_MAIN);
      }



      // kW (approx = V * I / 1000); negative allowed => regen
      // float kW = lastPacket.hv_voltage_V * lastPacket.hv_current_A / 1000.0f;
      // Map to your bar’s range [0..50] or symmetric if you prefer
      // int kw_bar_scaled = (int)roundf(kW * 100);                 // simple: -?

      // Serial.println("EEZ: kW = " + String(kW, 2) + " (" + String(kw_bar_scaled) + ")");

      // if(kW >= 0) {
        // if positive kw, start bar from bottom
        // lv_bar_set_start_value(objects.kw_bar, -300, LV_ANIM_OFF);
        // lv_bar_set_value(objects.kw_bar, kw_bar_scaled, LV_ANIM_OFF);
      // } else {
        // if negative kw, start bar from top
      //   lv_bar_set_start_value(objects.kw_bar, kw_bar_scaled, LV_ANIM_OFF);
      //   lv_bar_set_value(objects.kw_bar, 300, LV_ANIM_OFF);
      // }

      // lv_label_set_text_fmt(objects.kw_label, "%.2f\nkW", kW);
      
      // kw_bar = constrain(kw_bar, -50, 50);
      // If your bar only supports 0..50, offset negatives:
      // int kw_bar_u = kw_bar + 50;                   // 0..100
      // lv_bar_set_range(objects.kw_bar, 0, 100);
      // lv_bar_set_value(objects.kw_bar, kw_bar_u, LV_ANIM_OFF);
      // lv_label_set_text_fmt(objects.kw_label, "%.1f\nkW", kW);

      // (Add more bindings as needed)
      // e.g., coolant, SOC, temps:
      // lv_label_set_text_fmt(objects.coolant_label, "%.1f°C", lastPacket.ect_C);
      // lv_bar_set_value(objects.soc_bar, (int)roundf(lastPacket.soc_pct), LV_ANIM_OFF);

    } else {
      // stale: optionally gray-out or show placeholders
      // Example: keep last values but set a subtle status somewhere
      // lv_obj_add_state(objects.rpm_bar, LV_STATE_DISABLED);
      // lv_obj_add_state(objects.kw_bar, LV_STATE_DISABLED);
    }
  }
}

