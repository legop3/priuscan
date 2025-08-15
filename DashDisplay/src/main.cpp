/*
  Required Arduino Libraries:
  - LovyanGFX https://github.com/lovyan03/LovyanGFX
  - lvgl https://github.com/lvgl/lvgl
*/
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <lvgl.h>

//EEZ studio UI files
// #include "ui.h"
// #include "screens.h"

// #define EEZ_FOR_LVGL
extern "C" {
  #include "ui.h"
  // #include "screens.h"
}

#define GFX_BL  2   // Backlight pin

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// LovyanGFX Display Class
class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB     _bus_instance;
  lgfx::Panel_RGB   _panel_instance;

  LGFX(void) {
    {
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
      cfg.freq_write  = 15000000;

      // Sync timings
      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48;
      cfg.hsync_back_porch  = 40;
      
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 1;
      cfg.vsync_pulse_width = 31;
      cfg.vsync_back_porch  = 13;

      cfg.pclk_active_neg   = 1;
      cfg.de_idle_high      = 0;
      cfg.pclk_idle_high    = 0;

      _bus_instance.config(cfg);
    }

    {
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

// LVGL display buffer and driver
static uint32_t screenWidth;
static uint32_t screenHeight;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t disp_draw_buf[800 * 480 / 10];
static lv_disp_drv_t disp_drv;

/* LVGL flush callback */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  Serial.println("Flushing display...");
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  lcd.pushImageDMA(area->x1, area->y1, w, h, (lgfx::rgb565_t*)&color_p->full);
  lv_disp_flush_ready(disp);
}

/* Create a simple UI */
// void ui_init(void)
// {
//   lv_obj_t * main_screen = lv_obj_create(NULL);
//   lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x1e1e1e), LV_PART_MAIN);
//   lv_scr_load(main_screen);

//   lv_obj_t * title_label = lv_label_create(main_screen);
//   lv_label_set_text(title_label, "LVGL Display Demo (No Touch)");
//   lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);
//   lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
//   lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);

//   lv_obj_t * status_label = lv_label_create(main_screen);
//   lv_label_set_text(status_label, "Touch Disabled");
//   lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
//   lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 0);

//   lv_obj_t * info_label = lv_label_create(main_screen);
//   lv_label_set_text(info_label, "Static UI Example");
//   lv_obj_set_style_text_color(info_label, lv_color_white(), 0);
//   lv_obj_align(info_label, LV_ALIGN_BOTTOM_MID, 0, -20);

//   Serial.println("UI created successfully without touch!");
// }

void setup()
{
  Serial.begin(115200);
  Serial.println("LVGL Display Demo Starting...");

  lcd.begin();
  lcd.setTextSize(2);
  delay(200);
  
  lv_init();
  delay(100);

  // Backlight ON
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  screenWidth = lcd.width();
  screenHeight = lcd.height();
  
  Serial.printf("Screen resolution: %d x %d\n", screenWidth, screenHeight);

  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, screenWidth * screenHeight / 10);
  
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  delay(500);
  // Create UI
  ui_init();
  // Serial.println("UI initialized");

  // // Delete the current empty screen
  // lv_obj_t* old_screen = lv_scr_act();
  // Serial.printf("Deleting old screen: %p\n", old_screen);

  // lv_scr_load(objects.main);
  // lv_obj_del(old_screen); // Delete after loading new one

  // lv_timer_handler();
  // Serial.println("Old screen deleted, new screen loaded");

}

void loop()
{
  lv_timer_handler();
  // lv_task_handler();
  ui_tick();
  delay(5);
}