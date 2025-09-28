#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_NeoMatrix.h>

#include <WiFi.h>
#include <esp_now.h>

// bitmap file includes
#include "bitmapsBig/EV.xbm.h"
#include "bitmapsBig/GAS.xbm.h"
// #include "bitmapsBig/engine.xbm.h"
#include "bitmapsBig/M.xbm.h"


// matrix setup ------------------------------------------------------------------------------------------------
#define DATA_PIN    18      // LED data pin
#define W           16      // two 8x8 panels side by side
#define H            8
#define BRIGHTNESS   100     // initial brightness on boot
bool intro = true;

// confirmed layout flags
// BOT LEFT  COLS  PROG  => NEO_MATRIX_BOTTOM | NEO_MATRIX_LEFT | NEO_MATRIX_COLUMNS | NEO_MATRIX_PROGRESSIVE
Adafruit_NeoMatrix matrix(
  W, H, DATA_PIN,
  NEO_MATRIX_BOTTOM | NEO_MATRIX_LEFT | NEO_MATRIX_COLUMNS | NEO_MATRIX_PROGRESSIVE,
  NEO_GRB + NEO_KHZ800
);


// display functions ---------------------------------------------------------------------------------------

void showEv(bool m = false) {
  // matrix.fillScreen(0);
  matrix.fillRect(0, 1, W, H-1, 0); // clear everything except top rainbow line

  matrix.drawXBitmap(0, 0, EV_bits, EV_width, EV_height, matrix.Color(0, 255, 0));

  if (m) {
    matrix.drawXBitmap(0, 0, M_bits, M_width, M_height, matrix.Color(0, 100, 255));
  }

  matrix.show();
}

void showGas() {
  // matrix.fillScreen(0);
  matrix.fillRect(0, 1, W, H-1, 0); // clear everything except top rainbow line

  matrix.drawXBitmap(0, 0, GAS_bits, GAS_width, GAS_height, matrix.Color(255, 162, 0));

  matrix.show();
}


// data receiver setup ---------------------------------------------------------------------------------------
#define PACKET_TIMEOUT_MS 4000
volatile uint8_t last_flags = 0;
volatile unsigned long last_rx = 0;


void onDataRecv(const uint8_t*, const uint8_t* data, int len) {
  if (len < 1) return;
  last_flags = data[0];
  last_rx = millis();

  // Decode bits and print immediately on packet
  bool engine_on          = last_flags & (1 << 0);
  bool car_dim            = last_flags & (1 << 1);
  bool ev_mode            = last_flags & (1 << 2);
  bool display_off        = last_flags & (1 << 3);

  Serial.printf("RX flags: eng=%d, dim=%d, evm=%d, disp_off=%d\n",
                engine_on, car_dim, ev_mode, display_off);

  if (intro) {
    return;
  }

  if (engine_on == 1) {
    showGas();
  } else if (ev_mode == 1) {
    showEv(true);
  } else {
    showEv(false);
  }

  if (display_off == 1) {
    matrix.setBrightness(0);
  } else if (car_dim == 1) {
    matrix.setBrightness(10);
  } else {
    matrix.setBrightness(255);
  }
  

}


void setup() {

  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  Serial.print("ESP Board MAC Address: ");
  Serial.println(WiFi.macAddress());
  esp_now_init();
  esp_now_register_recv_cb(onDataRecv);

  pinMode(DATA_PIN, OUTPUT);
  digitalWrite(DATA_PIN, LOW);
  delay(20);

  matrix.begin();
  matrix.setBrightness(BRIGHTNESS);
  matrix.fillScreen(0);

  // matrix.drawPixel(0, 0, matrix.Color(255,   0,   0)); // 1st   = Red
  // matrix.drawPixel(15, 0, matrix.Color(  0, 255,   0)); // 2nd   = Green
  // matrix.drawPixel(15, 7, matrix.Color(  0,   0, 255)); // 65th  = Blue
  // matrix.drawPixel(0, 7, matrix.Color(255, 255,   0)); // 66th  = Yellow

  // matrix.show();
  // delay(1000);

  // show fullscreen rainbow on start
  matrix.drawLine(0, 0, W-1, 0, matrix.Color(255,   0,   0)); // red
  matrix.drawLine(0, 1, W-1, 1, matrix.Color(255, 127,   0)); // orange
  matrix.drawLine(0, 2, W-1, 2, matrix.Color(255, 255,   0)); // yellow
  matrix.drawLine(0, 3, W-1, 3, matrix.Color(0,   255,   0)); // green
  matrix.drawLine(0, 4, W-1, 4, matrix.Color(0,     0, 255)); // blue
  matrix.drawLine(0, 5, W-1, 5, matrix.Color(75,    0, 130)); // indigo
  matrix.drawLine(0, 6, W-1, 6, matrix.Color(148,    0, 211)); // violet
  
  matrix.show();
  delay(1000);

  // show more important colors on left side :3
  matrix.drawLine(0, 0, 0, 7, matrix.Color(0,   255, 255)); // light blue
  matrix.drawLine(1, 0, 1, 7, matrix.Color(255, 105, 180)); // pink
  matrix.drawLine(2, 0, 2, 7, matrix.Color(255, 255, 255)); // white
  matrix.drawLine(3, 0, 3, 7, matrix.Color(255, 105, 180)); // pink
  matrix.drawLine(4, 0, 4, 7, matrix.Color(0,   255, 255)); // light blue

  matrix.show();
  delay(3000);

  matrix.fillScreen(0);

  // permanent rainbow across top row, 2 pixel wide lines
  // matrix.drawLine(0, 0, 2, 0, matrix.Color(255,   0,   0)); // red
  // matrix.drawLine(3, 0, 4, 0, matrix.Color(255, 127,   0)); // orange
  // matrix.drawLine(5, 0, 6, 0, matrix.Color(255, 255,   0)); // yellow
  // matrix.drawLine(7, 0, 8, 0, matrix.Color(0,   255,   0)); // green
  // matrix.drawLine(9, 0, 10, 0, matrix.Color(0,     0, 255)); // blue
  // matrix.drawLine(11, 0, 12, 0, matrix.Color(75,    0, 130)); // indigo
  // matrix.drawLine(13, 0, 15, 0, matrix.Color(148,    0, 211)); // violet


  matrix.show();
  delay(2000);

  intro = false;
}



void loop() {
  // show ev without m
  // showEv();
  // delay(3000);

  // show ev with m
  // showEv(true);
  // delay(3000);

  // show gas mode
  // showGas();
  // delay(3000);

  // show engine icon
  // showEngineIcon();
  // delay(4000);

  // bool stale = (PACKET_TIMEOUT_MS > 0) && (millis() - last_rx > PACKET_TIMEOUT_MS);
  // uint8_t f = stale ? 0 : last_flags;




  // static unsigned long last_print = 0;
  //   if (millis() - last_print >= 1000) {
  //     last_print = millis();
  //     if (stale) {
  //       Serial.println("No packets recently :3 flags considered 0");
  //     } else {
  //       // Serial.printf("Flags now: 0b" BYTE_TO_BINARY_PATTERN "\n",
  //       //               BYTE_TO_BINARY(f)); 
  //     }


  //       Serial.print((f & (1 << 0)) ? "E" : "-");
  //       Serial.print((f & (1 << 1)) ? "D" : "-");
  //       Serial.print((f & (1 << 2)) ? "V" : "-");
  //       Serial.println((f & (1 << 3)) ? "O" : "-");

  //   }
  




}
