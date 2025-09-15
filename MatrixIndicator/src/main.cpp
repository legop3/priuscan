#include <WiFi.h>
#include <esp_now.h>

#define LED_PIN GPIO_NUM_2  // onboard LED
#define PACKET_TIMEOUT_MS 4000  // 0 to disable timeout

volatile uint8_t engine_on = 0;
volatile unsigned long last_rx = 0;

void onDataRecv(const uint8_t*, const uint8_t* data, int len) {
  if (len > 0) {
    engine_on = data[0] ? 1 : 0;
    last_rx = millis();
    Serial.print("Packet received: engine_on = ");
    Serial.println(engine_on);
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  Serial.print("Receiver MAC: ");
  Serial.println(WiFi.macAddress());
  esp_now_init();
  esp_now_register_recv_cb(onDataRecv);
}

void loop() {
  bool stale = (PACKET_TIMEOUT_MS > 0) && (millis() - last_rx > PACKET_TIMEOUT_MS);
  bool led_state = (!stale && engine_on);

  digitalWrite(LED_PIN, led_state ? HIGH : LOW);

  // Print timeout status occasionally
  // static unsigned long last_print = 0;
  // if (millis() - last_print > 1000) {
  //   last_print = millis();
  //   if (stale) {
  //     Serial.println("No packets recently → treating engine as OFF");
  //   } else {
  //     Serial.print("Engine status: ");
  //     Serial.println(led_state ? "ON" : "OFF");
  //   }
  // }
}
