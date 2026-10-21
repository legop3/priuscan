#include "matrix_indicator.h"

#include <WiFi.h>
#include <esp_now.h>
#include <limits.h>

namespace {

constexpr uint8_t PEER_MAC[] = {0x34, 0x98, 0x7A, 0x5D, 0xDE, 0xF8};
constexpr float ENGINE_ON_RPM = 500.0f;
constexpr int8_t EBAR_WARN_ON = 48;
constexpr unsigned long SEND_INTERVAL_MS = 500;

uint8_t lastFlags = 0xFF;
int8_t lastEbarStep = INT8_MIN;
unsigned long lastSend = 0;

int8_t calculateEbarStep(int8_t ebar, bool engineOn) {
  if (engineOn) return 0;

  if (ebar < 0) {
    int mag = -ebar;
    if (mag > 100) mag = 100;
    int lit = (mag * 16) / 100;
    if (lit < 0) lit = 0;
    if (lit > 16) lit = 16;
    return (int8_t)-lit;
  }

  int pos = ebar;
  if (pos < 0) pos = 0;
  if (pos > 100) pos = 100;
  int lit = (pos * 16) / EBAR_WARN_ON;
  if (lit < 0) lit = 0;
  if (lit > 16) lit = 16;
  return (int8_t)lit;
}

}  // namespace

void initMatrixIndicator() {
  WiFi.mode(WIFI_STA);
  esp_now_init();

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, PEER_MAC, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  Serial.println(" ESP-NOW.............INIT");
}

void updateMatrixIndicator(float rpm, int8_t ebar, bool carDim, bool evMode, bool displayOff) {
  uint8_t engineOn = (rpm > ENGINE_ON_RPM) ? 1 : 0;
  uint8_t flags =
      (engineOn << 0) |
      ((carDim ? 1 : 0) << 1) |
      ((evMode ? 1 : 0) << 2) |
      ((displayOff ? 1 : 0) << 3);

  int8_t ebarStep = calculateEbarStep(ebar, engineOn);

  unsigned long now = millis();
  if (flags != lastFlags || ebarStep != lastEbarStep || (now - lastSend) >= SEND_INTERVAL_MS) {
    uint8_t packet[2] = {flags, (uint8_t)ebarStep};
    esp_now_send(PEER_MAC, packet, sizeof(packet));
    lastFlags = flags;
    lastEbarStep = ebarStep;
    lastSend = now;
  }
}
