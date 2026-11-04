#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_NeoMatrix.h>
#include <WiFi.h>
#include <esp_now.h>
#include "neon_badges.h"

constexpr uint8_t DATA_PIN = 14;
constexpr uint8_t BOOT_BRIGHTNESS = 100;
Adafruit_NeoMatrix matrix(
    neon::WIDTH, neon::HEIGHT, DATA_PIN,
    NEO_MATRIX_BOTTOM | NEO_MATRIX_LEFT | NEO_MATRIX_COLUMNS | NEO_MATRIX_PROGRESSIVE,
    NEO_GRB + NEO_KHZ800);

// ESP-NOW runs in another task. Publish a coherent packet; all drawing and LED
// transmission happens in loop(), never inside the receive callback.
portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
matrix_indicator::State pendingState{};
int16_t pendingWireEbar = 0;
uint32_t rxRevision = 0;

void onDataRecv(const uint8_t*, const uint8_t* data, int len) {
  matrix_indicator::WireState next{};
  if (len < 0 || !matrix_indicator::decodePacket(data,static_cast<size_t>(len),next)) return;
  portENTER_CRITICAL(&rxMux);
  pendingWireEbar = next.ebar;
  pendingState = matrix_indicator::displayState(next);
  ++rxRevision;
  portEXIT_CRITICAL(&rxMux);
}

neon::Renderer renderer;
neon::Frame frame;
uint32_t startupAt = 0;
uint32_t lastFrameAt = 0;
uint32_t consumedRevision = 0;
matrix_indicator::State currentState{};
bool haveLiveState = false;
bool idleCleared = false;

void showFrame() {
  // RGB888 passthrough retains the proposal's dim sheen and white glint without
  // RGB565 quantization. NeoMatrix still handles the confirmed physical layout.
  for (int y=0; y<neon::HEIGHT; ++y) for (int x=0; x<neon::WIDTH; ++x) {
    const neon::Color c = frame.at(x,y);
    matrix.setPassThruColor((static_cast<uint32_t>(c.r)<<16) |
        (static_cast<uint32_t>(c.g)<<8) | c.b);
    matrix.drawPixel(x,y,0);
  }
  matrix.setPassThruColor();
  matrix.show();
}

void setup() {
  Serial.begin(115200);
  pinMode(DATA_PIN,OUTPUT);
  digitalWrite(DATA_PIN,LOW);
  delay(20);
  matrix.begin();
  // Global dim/off is applied to the freshly rendered frame, avoiding repeated
  // destructive setBrightness() rescaling and preserving colors on restoration.
  matrix.setBrightness(255);
  matrix.fillScreen(0);
  matrix.show();

  WiFi.mode(WIFI_STA);
  Serial.print("ESP Board MAC Address: ");
  Serial.println(WiFi.macAddress());
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onDataRecv);
  } else {
    Serial.println("ESP-NOW initialization failed");
  }
  startupAt = millis();
  lastFrameAt = startupAt-neon::FRAME_MS;
}

void loop() {
  const uint32_t now = millis();
  matrix_indicator::State received;
  uint32_t revision;
  int16_t wireEbar;
  portENTER_CRITICAL(&rxMux);
  received = pendingState;
  revision = rxRevision;
  wireEbar = pendingWireEbar;
  portEXIT_CRITICAL(&rxMux);

  // Observe the received count outside the Wi-Fi callback. Keep this paired
  // with the sender trace until the physical half-width discrepancy is located.
  static uint32_t lastTrace = 0;
  if (revision != 0 && static_cast<uint32_t>(now-lastTrace) >= 500) {
    Serial.printf("Matrix BAR RX ebar=%d engine=%u pixels=%d\n",
        wireEbar, (received.flags & matrix_indicator::ENGINE) ? 1 : 0, received.step);
    lastTrace = now;
  }

  bool urgent = false;
  if (revision != consumedRevision) {
    consumedRevision = revision;
    urgent = !haveLiveState ||
        ((received.flags ^ currentState.flags) & matrix_indicator::ENGINE) ||
        matrix_indicator::brightness(received) != matrix_indicator::brightness(currentState);
    renderer.update(received,now);
    currentState = received;
    haveLiveState = true; // Buffer live state while the startup sequence finishes.
  }

  // Off and brightness changes bypass the 50 ms frame deadline. Repeated OFF
  // packets do not retransmit black or run unnecessary animation frames.
  if (haveLiveState && (currentState.flags & matrix_indicator::OFF) && !urgent) {
    delay(1);
    return;
  }
  if (!urgent && static_cast<uint32_t>(now-lastFrameAt) < neon::FRAME_MS) {
    delay(1);
    return;
  }
  lastFrameAt = now; // Skip missed deadlines; never burst through catch-up frames.

  if (haveLiveState && (currentState.flags & matrix_indicator::OFF)) {
    frame.clear();
  } else if (static_cast<uint32_t>(now-startupAt) < neon::STARTUP_MS) {
    // Incoming telemetry stays current but cannot truncate the boot animation.
    // Dim/off still takes effect immediately during the intro.
    neon::startup(frame,now-startupAt);
    const uint8_t liveBrightness = haveLiveState ?
        matrix_indicator::brightness(currentState) : 255;
    const uint8_t brightness = liveBrightness < BOOT_BRIGHTNESS ?
        liveBrightness : BOOT_BRIGHTNESS;
    for (auto& p : frame.pixels) p=neon::scale(p,brightness,255);
  } else if (haveLiveState) {
    renderer.render(frame,now);
  } else {
    // Match existing no-data behavior: blank after intro until the first packet.
    // After reception retain the latest state; no new disconnect policy is added.
    if (idleCleared) { delay(1); return; }
    frame.clear();
    idleCleared = true;
  }
  showFrame();
}
