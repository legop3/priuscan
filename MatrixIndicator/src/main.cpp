#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_NeoMatrix.h>

// bitmap file includes
#include "bitmapsBig/EV.xbm.h"
#include "bitmapsBig/GAS.xbm.h"
// #include "bitmapsBig/engine.xbm.h"
#include "bitmapsBig/M.xbm.h"


#define DATA_PIN    18      // LED data pin
#define W           16      // two 8x8 panels side by side
#define H            8
#define BRIGHTNESS   10     // initial brightness on boot

// confirmed layout flags
// BOT LEFT  COLS  PROG  => NEO_MATRIX_BOTTOM | NEO_MATRIX_LEFT | NEO_MATRIX_COLUMNS | NEO_MATRIX_PROGRESSIVE
Adafruit_NeoMatrix matrix(
  W, H, DATA_PIN,
  NEO_MATRIX_BOTTOM | NEO_MATRIX_LEFT | NEO_MATRIX_COLUMNS | NEO_MATRIX_PROGRESSIVE,
  NEO_GRB + NEO_KHZ800
);

void setup() {
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
}

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

// void showEngineIcon() {
//   matrix.fillScreen(0);

//   matrix.drawXBitmap(0, 0, engine_bits, engine_width, engine_height, matrix.Color(255, 162, 0));

//   matrix.show();
// }



void loop() {
  // show ev without m
  showEv();
  delay(3000);

  // show ev with m
  showEv(true);
  delay(3000);

  // show gas mode
  showGas();
  delay(3000);

  // show engine icon
  // showEngineIcon();
  // delay(4000);
}
