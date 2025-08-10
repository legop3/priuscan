/* Using LVGL with Arduino requires some extra steps...
 *  
 * Be sure to read the docs here: https://docs.lvgl.io/master/integration/framework/arduino.html
 * but note you should use the lv_conf.h from the repo as it is pre-edited to work.
 * 
 * You can always edit your own lv_conf.h later and exclude the example options once the build environment is working.
 * 
 * Note you MUST move the 'examples' and 'demos' folders into the 'src' folder inside the lvgl library folder 
 * otherwise this will not compile, please see README.md in the repo.
 * 
 */
#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <examples/lv_examples.h>
#include <demos/lv_demos.h>
#include <XPT2046_Touchscreen.h>

// A library for interfacing with the touch screen
//
// Can be installed from the library manager (Search for "XPT2046")
//https://github.com/PaulStoffregen/XPT2046_Touchscreen
// ----------------------------
// Touch Screen pins
// ----------------------------

// The CYD touch uses some non default SPI pins
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

SPIClass touchscreenSpi = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);
uint16_t touchScreenMinimumX = 200, touchScreenMaximumX = 3700, touchScreenMinimumY = 240, touchScreenMaximumY = 3800;

/*Set to your screen resolution*/
#define TFT_HOR_RES   320
#define TFT_VER_RES   240

/*LVGL draw into this buffer, 1/10 screen size usually works well. The size is in bytes*/
#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES / 10 * (LV_COLOR_DEPTH / 8))

// Initialize TFT_eSPI
TFT_eSPI tft = TFT_eSPI();

#if LV_USE_LOG != 0
void my_print( lv_log_level_t level, const char * buf )
{
    LV_UNUSED(level);
    Serial.println(buf);
    Serial.flush();
}
#endif

/* LVGL calls it when a rendered image needs to copied to the display*/
void my_disp_flush( lv_display_t *disp, const lv_area_t *area, uint8_t * px_map)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)px_map, w * h, true);
    tft.endWrite();

    /*Call it to tell LVGL you are ready*/
    lv_disp_flush_ready(disp);
}

/*Read the touchpad*/
void my_touchpad_read( lv_indev_t * indev, lv_indev_data_t * data )
{
  if(touchscreen.touched())
  {
    TS_Point p = touchscreen.getPoint();
    //Some very basic auto calibration so it doesn't go out of range
    if(p.x < touchScreenMinimumX) touchScreenMinimumX = p.x;
    if(p.x > touchScreenMaximumX) touchScreenMaximumX = p.x;
    if(p.y < touchScreenMinimumY) touchScreenMinimumY = p.y;
    if(p.y > touchScreenMaximumY) touchScreenMaximumY = p.y;
    //Map this to the pixel position
    data->point.x = map(p.x,touchScreenMinimumX,touchScreenMaximumX,1,TFT_HOR_RES); /* Touchscreen X calibration */
    data->point.y = map(p.y,touchScreenMinimumY,touchScreenMaximumY,1,TFT_VER_RES); /* Touchscreen Y calibration */
    data->state = LV_INDEV_STATE_PRESSED;
    /*
    Serial.print("Touch x ");
    Serial.print(data->point.x);
    Serial.print(" y ");
    Serial.println(data->point.y);
    */
  }
  else
  {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

lv_indev_t * indev; //Touchscreen input device
uint8_t* draw_buf;  //draw_buf is allocated on heap otherwise the static area is too big on ESP32 at compile
uint32_t lastTick = 0;  //Used to track the tick timer

void setup()
{
  //Some basic info on the Serial console
  String LVGL_Arduino = "LVGL demo ";
  LVGL_Arduino += String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();
  Serial.begin(115200);
  Serial.println(LVGL_Arduino);
  
  //Initialize the display first
  tft.begin();
  tft.setRotation(1); // Landscape orientation - try 1, 2, 3 if this doesn't look right
  tft.fillScreen(TFT_BLACK);
    
  //Initialise the touchscreen
  touchscreenSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS); /* Start second SPI bus for touchscreen */
  touchscreen.begin(touchscreenSpi); /* Touchscreen init */
  touchscreen.setRotation(1); /* Inverted landscape orientation to match screen */

  //Initialise LVGL
  lv_init();

#if LV_USE_LOG != 0
  lv_log_register_print_cb( my_print ); /* register print function for debugging */
#endif

  // Create display
  lv_display_t * disp = lv_display_create(TFT_HOR_RES, TFT_VER_RES);
  lv_display_set_flush_cb(disp, my_disp_flush);
  
  // Allocate draw buffer using LVGL 9 API
  draw_buf = new uint8_t[DRAW_BUF_SIZE];
  lv_display_set_buffers(disp, draw_buf, NULL, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

  //Initialize the XPT2046 input device driver
  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);  
  lv_indev_set_read_cb(indev, my_touchpad_read);
  
  //Or try out the large standard widgets demo
  // lv_demo_widgets();
  // lv_demo_benchmark();          
  // lv_demo_keypad_encoder();     
///////////////////////////////////////////////////////////////////////

  lv_obj_t * scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);


  // Disable scrolling entirely for the screen
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Force the screen to use grid layout and match display size
  lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);

  // Define a 3-column × 2-row grid, each cell is equal percentage of the screen
  static lv_coord_t col_dsc[] = {LV_PCT(33), LV_PCT(33), LV_PCT(34), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row_dsc[] = {LV_PCT(50), LV_PCT(50), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(scr, col_dsc, row_dsc);

  // Button 1
  lv_obj_t * btn1 = lv_btn_create(scr);
  lv_obj_set_style_bg_color(btn1, lv_color_hex(0xFF0000), LV_PART_MAIN); // Red background
  lv_obj_set_style_bg_color(btn1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_PRESSED); // White background when pressed
  lv_obj_set_grid_cell(btn1, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_t * lbl1 = lv_label_create(btn1);
  lv_label_set_text(lbl1, "Play");
  lv_obj_center(lbl1);

  // Button 2
  lv_obj_t * btn2 = lv_btn_create(scr);
  lv_obj_set_style_bg_color(btn2, lv_color_hex(0x00A605), LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn2, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_grid_cell(btn2, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_t * lbl2 = lv_label_create(btn2);
  lv_label_set_text(lbl2, "Pause");
  lv_obj_center(lbl2);

  // Button 3
  lv_obj_t * btn3 = lv_btn_create(scr);
  lv_obj_set_style_bg_color(btn3, lv_color_hex(0x00FFDE), LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn3, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_grid_cell(btn3, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_t * lbl3 = lv_label_create(btn3);
  lv_label_set_text(lbl3, "Stop");
  lv_obj_center(lbl3);

  // Button 4
  lv_obj_t * btn4 = lv_btn_create(scr);
  lv_obj_set_style_bg_color(btn4, lv_color_hex(0xAA00FF), LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn4, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_grid_cell(btn4, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_t * lbl4 = lv_label_create(btn4);
  lv_label_set_text(lbl4, "Rewind");
  lv_obj_center(lbl4);

  // Button 5
  lv_obj_t * btn5 = lv_btn_create(scr);
  lv_obj_set_style_bg_color(btn5, lv_color_hex(0xFF00E6), LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn5, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_grid_cell(btn5, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_t * lbl5 = lv_label_create(btn5);
  lv_label_set_text(lbl5, "Forward");
  lv_obj_center(lbl5);

  // Button 6
  lv_obj_t * btn6 = lv_btn_create(scr);
  lv_obj_set_style_bg_color(btn6, lv_color_hex(0xFF9900), LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn6, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_grid_cell(btn6, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_t * lbl6 = lv_label_create(btn6);
  lv_label_set_text(lbl6, "Record");
  lv_obj_center(lbl6);
/////////////////////////////////////////////////////////////
  Serial.println( "Setup done" );
}

void loop()
{   
    lv_tick_inc(millis() - lastTick); //Update the tick timer. Tick is new for LVGL 9
    lastTick = millis();
    lv_timer_handler();               //Update the UI
    delay(5);
}