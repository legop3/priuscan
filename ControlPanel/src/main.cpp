#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

// Display object
TFT_eSPI tft = TFT_eSPI();

// Touch detection variables
bool touchEnabled = false;
unsigned long lastTouch = 0;

// Function declarations
void drawButtons();
void drawButton(int buttonIndex);
void handleTouch(uint16_t touchX, uint16_t touchY);
void onButtonPress(int buttonId);
void calibrateTouch();
void scanTouchController();

// Button structure
struct Button {
  int x, y, w, h;
  String label;
  uint16_t color;
  uint16_t textColor;
  bool pressed;
  int id;
};

// Define 6 buttons in a 2x3 grid - adjusted for landscape mode
Button buttons[6] = {
  {20, 60, 90, 50, "BTN 1", TFT_BLUE, TFT_WHITE, false, 1},
  {120, 60, 90, 50, "BTN 2", TFT_RED, TFT_WHITE, false, 2},
  {220, 60, 90, 50, "BTN 3", TFT_GREEN, TFT_BLACK, false, 3},
  {20, 120, 90, 50, "BTN 4", TFT_YELLOW, TFT_BLACK, false, 4},
  {120, 120, 90, 50, "BTN 5", TFT_MAGENTA, TFT_WHITE, false, 5},
  {220, 120, 90, 50, "BTN 6", TFT_CYAN, TFT_BLACK, false, 6}
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Touch Screen Debug ===");
  
  // Initialize display
  tft.init();
  Serial.println("Display initialized");
  
  // Turn on backlight
  #ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  Serial.println("Backlight turned on");
  #endif
  
  tft.setRotation(1); // Landscape mode
  tft.fillScreen(TFT_BLACK);
  
  // Draw title
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Touch Debug", 10, 10);
  
  // Scan for touch controller
  scanTouchController();
  
  // Try to calibrate touch
  calibrateTouch();
  
  // Draw interface
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Control Panel", 10, 10);
  
  drawButtons();
  
  Serial.println("Setup complete - touch test active");
}

void loop() {
  uint16_t x, y;
  
  // Try multiple touch detection methods
  bool touched = false;
  
  // Method 1: Standard getTouch
  touched = tft.getTouch(&x, &y);
  
  if (!touched) {
    // Method 2: Try getTouchRaw
    touched = tft.getTouchRaw(&x, &y);
    if (touched) {
      Serial.print("Raw: ");
    }
  }
  
  if (!touched) {
    // Method 3: Check touch with different timing
    delay(1);
    touched = tft.getTouch(&x, &y);
  }
  
  if (touched && (millis() - lastTouch > 300)) {
    lastTouch = millis();
    
    Serial.print("Touch: ");
    Serial.print(x);
    Serial.print(", ");
    Serial.print(y);
    
    // Show touch on screen
    tft.fillCircle(x % 320, y % 240, 4, TFT_WHITE);
    
    // Check if coordinates make sense
    if (x > 50 && x < 4000 && y > 50 && y < 4000) {
      Serial.println(" (RAW - needs calibration)");
      // Try to map raw coordinates to screen coordinates
      int screenX = map(x, 200, 3800, 0, 320);
      int screenY = map(y, 200, 3800, 0, 240);
      Serial.print("Mapped to: ");
      Serial.print(screenX);
      Serial.print(", ");
      Serial.println(screenY);
      
      if (screenX >= 0 && screenX < 320 && screenY >= 0 && screenY < 240) {
        tft.fillCircle(screenX, screenY, 4, TFT_GREEN);
        handleTouch(screenX, screenY);
      }
    } else if (x < 320 && y < 240) {
      Serial.println(" (CALIBRATED)");
      handleTouch(x, y);
    } else {
      Serial.println(" (INVALID)");
    }
  }
  
  delay(10);
}

void scanTouchController() {
  Serial.println("\n=== Scanning for Touch Controller ===");
  
  // Test different SPI configurations
  int csPins[] = {21, 33, 5, 27, 32, 4, 16, 17};
  
  for (int i = 0; i < 8; i++) {
    Serial.print("Testing CS pin ");
    Serial.print(csPins[i]);
    Serial.print("... ");
    
    pinMode(csPins[i], OUTPUT);
    digitalWrite(csPins[i], HIGH);
    delay(10);
    
    uint16_t x, y;
    if (tft.getTouch(&x, &y)) {
      Serial.print("Response: ");
      Serial.print(x);
      Serial.print(", ");
      Serial.println(y);
    } else {
      Serial.println("No response");
    }
  }
  
  Serial.println("=== End Touch Scan ===\n");
}

void calibrateTouch() {
  Serial.println("Starting touch calibration...");
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.drawString("Touch corners", 50, 100);
  
  delay(2000);
  
  // Wait for touch in each corner to calibrate
  Serial.println("Touch calibration skipped for now");
  Serial.println("Will use auto-mapping in main loop");
}

void drawButtons() {
  for (int i = 0; i < 6; i++) {
    drawButton(i);
  }
}

void drawButton(int buttonIndex) {
  Button &btn = buttons[buttonIndex];
  
  // Draw button background
  uint16_t bgColor = btn.pressed ? TFT_DARKGREY : btn.color;
  tft.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 5, bgColor);
  
  // Draw button border
  tft.drawRoundRect(btn.x, btn.y, btn.w, btn.h, 5, TFT_WHITE);
  
  // Draw button text
  tft.setTextColor(btn.textColor);
  tft.setTextSize(1);
  
  // Center text in button
  int textX = btn.x + (btn.w - btn.label.length() * 6) / 2;
  int textY = btn.y + (btn.h - 8) / 2;
  
  tft.drawString(btn.label, textX, textY);
  
  // Draw button ID in corner
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.drawString(String(btn.id), btn.x + 5, btn.y + 5);
}

void handleTouch(uint16_t touchX, uint16_t touchY) {
  for (int i = 0; i < 6; i++) {
    Button &btn = buttons[i];
    
    // Check if touch is within button bounds
    if (touchX >= btn.x && touchX <= (btn.x + btn.w) &&
        touchY >= btn.y && touchY <= (btn.y + btn.h)) {
      
      // Button pressed
      btn.pressed = true;
      drawButton(i);
      
      // Handle button action
      onButtonPress(btn.id);
      
      // Visual feedback
      delay(100);
      
      // Release button
      btn.pressed = false;
      drawButton(i);
      
      break; // Only handle one button press at a time
    }
  }
}

void onButtonPress(int buttonId) {
  Serial.print("=== BUTTON ");
  Serial.print(buttonId);
  Serial.println(" PRESSED! ===");
  
  // Add your custom actions here
  switch (buttonId) {
    case 1:
      Serial.println("Action: Turn on LED");
      // digitalWrite(LED_PIN, HIGH);
      break;
      
    case 2:
      Serial.println("Action: Turn off LED");
      // digitalWrite(LED_PIN, LOW);
      break;
      
    case 3:
      Serial.println("Action: Start motor");
      // startMotor();
      break;
      
    case 4:
      Serial.println("Action: Stop motor");
      // stopMotor();
      break;
      
    case 5:
      Serial.println("Action: Read sensor");
      // readSensorValue();
      break;
      
    case 6:
      Serial.println("Action: Reset system");
      // resetSystem();
      break;
  }
}