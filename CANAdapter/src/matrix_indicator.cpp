#include "matrix_indicator.h"

#include <WiFi.h>
#include <esp_now.h>
#include <limits.h>
#include "../../shared/matrix_protocol.h"

namespace {
constexpr uint8_t PEER_MAC[] = {0x34, 0x98, 0x7A, 0x5D, 0xDE, 0xF8};
constexpr unsigned long SEND_INTERVAL_MS = 500;
uint8_t lastFlags = 0xFF;
int16_t lastEbarValue = INT16_MIN;
unsigned long lastSend = 0;
} // namespace

void initMatrixIndicator() {
  WiFi.mode(WIFI_STA);
  esp_now_init();
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr,PEER_MAC,6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
  Serial.println(" ESP-NOW.............INIT");
}

void updateMatrixIndicator(float rpm, int16_t ebar, bool carDim, bool evMode, bool displayOff) {
  const matrix_indicator::WireState state = matrix_indicator::makeWireState(rpm,ebar,carDim,evMode,displayOff);
  const unsigned long now = millis();
  if (state.flags != lastFlags || state.ebar != lastEbarValue || now-lastSend >= SEND_INTERVAL_MS) {
    uint8_t packet[matrix_indicator::PACKET_SIZE];
    matrix_indicator::encodePacket(state,packet);
    esp_now_send(PEER_MAC,packet,sizeof(packet));
    lastFlags = state.flags;
    lastEbarValue = state.ebar;
    lastSend = now;
  }
}
