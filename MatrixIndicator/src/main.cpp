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

  matrix.drawPixel(0, 0, matrix.Color(255,   0,   0)); // 1st   = Red
  matrix.drawPixel(15, 0, matrix.Color(  0, 255,   0)); // 2nd   = Green
  matrix.drawPixel(15, 7, matrix.Color(  0,   0, 255)); // 65th  = Blue
  matrix.drawPixel(0, 7, matrix.Color(255, 255,   0)); // 66th  = Yellow

  matrix.show();
  delay(1000);
  matrix.fillScreen(0);
  matrix.show();
}

void showEv(bool m = false) {
  matrix.fillScreen(0);

  matrix.drawXBitmap(0, 0, EV_bits, EV_width, EV_height, matrix.Color(0, 255, 0));

  if (m) {
    matrix.drawXBitmap(0, 0, M_bits, M_width, M_height, matrix.Color(0, 100, 255));
  }

  matrix.show();
}

void showGas() {
  matrix.fillScreen(0);

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
