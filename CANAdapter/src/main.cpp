#include <Arduino.h>
#include <esp32_can.h> /* https://github.com/collin80/esp32_can */
#include <WiFi.h>
#include <esp_now.h>

#include "can_tx.h"
#include "steering_controls.h"

// ploo woo goo woo


#define SHIELD_LED_PIN 26

// MAC address of the indicator ESP
uint8_t PEER_MAC[] = {0x34, 0x98, 0x7A, 0x5D, 0xDE, 0xF8};
// globals for esp-now stuff
uint8_t last_flags = 0xFF;
unsigned long last_send = 0;


// ====== SIMPLE UART: FLOATS, NO SCALING ======
#include <HardwareSerial.h>
HardwareSerial& DISP = Serial2;

// Pick pins & baud for your wiring
static const int UART2_RX_PIN = 16;
static const int UART2_TX_PIN = 17;
static const uint32_t UART_BAUD = 230400; // can raise later if link is clean



/////////////////////////////////////////////////////////utility functions////////////////////////////////////////////////////

// Inline for performance-critical function
inline uint16_t Process_Endian(uint8_t byte_msb, uint8_t byte_lsb) {
  return (byte_msb << 8) | byte_lsb;
}

void sendCANFrame(uint32_t canID, const uint8_t* data, uint8_t dataLength, bool extended, bool rtr) {
    // Use a local frame so back-to-back sends don't overwrite a shared TX buffer.
    CAN_FRAME txFrame = {};
    txFrame.id = canID;
    txFrame.length = dataLength;
    txFrame.extended = extended;
    txFrame.rtr = rtr;

    // Use memcpy for faster bulk copy when dataLength > 4
    uint8_t copyLength = (dataLength > 8) ? 8 : dataLength;
    if (dataLength >= 4) {
        memcpy(txFrame.data.byte, data, copyLength);
    } else {
        // Manual copy for small arrays (often faster than memcpy overhead)
        for (int i = 0; i < copyLength; i++) {
            txFrame.data.byte[i] = data[i];
        }
    }

    // Clear remaining bytes if needed
    if (dataLength < 8) {
        memset(txFrame.data.byte + dataLength, 0, 8 - dataLength);
    }

    CAN0.sendFrame(txFrame);
}

// helper: Flow Control (CTS)
inline void sendFlowControl(uint32_t req_id, uint8_t blockSize=0x00, uint8_t stMin=0x00) {
  uint8_t fc[8] = {0x30, blockSize, stMin, 0,0,0,0,0}; // PCI=FC/CTS
  sendCANFrame(req_id, fc, 8); // FC goes to the ECU's receive/request ID, e.g. 0x7E2.
}

// Optimized overload using stack array
void sendCANFrame(uint32_t canID, std::initializer_list<uint8_t> data, bool extended, bool rtr) {
    uint8_t dataArray[8];
    uint8_t length = (data.size() > 8) ? 8 : data.size();

    auto it = data.begin();
    for (uint8_t i = 0; i < length; ++i, ++it) {
        dataArray[i] = *it;
    }

    sendCANFrame(canID, dataArray, length, extended, rtr);
}

// Clean sensor request function - no more duplication!
void sendSensorRequest(uint8_t sensorNum) {
    switch (sensorNum) {
        case 0: {
            uint8_t req[] = {0x02, 0x21, 0x98, 0x00, 0x00, 0x00, 0x00, 0x00}; // Battery Current
            sendCANFrame(0x7E2, req, 8);
            break;
        }
        case 1: {
            uint8_t req[] = {0x02, 0x21, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00}; // Battery Voltage
            sendCANFrame(0x7E2, req, 8);
            // No FC here—decoder will send FC to 0x7EA only when FF 61 74 is seen
            break;
        }
        case 2: {
            uint8_t req[] = {0x02, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00}; // Coolant Temp
            sendCANFrame(0x7E2, req, 8);
            break;
        }
        case 3: {  // HV battery temps + intake temp (21 87)
            uint8_t req[] = {0x02, 0x21, 0x87, 0,0,0,0,0};
            sendCANFrame(0x7E2, req, 8);
            // no FC here; we’ll send FC from the decoder when FF 61 87 arrives
            break;
        }
        case 4: {
            uint8_t req[] = {0x02, 0x01, 0x5B, 0x00, 0x00, 0x00, 0x00, 0x00}; // HV battery SoC
            sendCANFrame(0x7E2, req, 8);
            break;
        }
        case 5: {
            // HV battery fan mode
            uint8_t req[] = {0x02, 0x21, 0x9B, 0x00, 0x00, 0x00, 0x00, 0x00};
            sendCANFrame(0x7E2, req, 8);
            break;
        }
    }
}

/////////////////////////////////////////////////////////////global variables//////////////////////////////////////////////////////////
uint8_t fanOverrideEnable = 0;

// Sensor polling stuff
volatile float g_sensors[20] = {0}; // All sensor values
enum : uint8_t {
  SENSOR_HV_CURRENT = 0,
  SENSOR_HV_VOLTAGE = 1,
  SENSOR_ECT = 2,
  SENSOR_HV_TEMPS = 3,
  SENSOR_SOC = 4,
  SENSOR_HV_FAN_MODE = 5,
  SENSOR_COUNT = 6,
  SENSOR_NONE = 0xFF
};
bool waiting = false;               // Are we waiting for a response?
uint8_t currentPollSensor = SENSOR_NONE;
unsigned long requestTimeout = 0;   // When we sent the last request

const uint8_t fastSensors[] = {
  SENSOR_HV_CURRENT,
  SENSOR_HV_VOLTAGE
};

const uint8_t slowSensors[] = {
  SENSOR_ECT,
  SENSOR_HV_TEMPS,
  SENSOR_SOC,
  SENSOR_HV_FAN_MODE
};

const uint8_t FAST_POLLS_BETWEEN_SLOW_POLLS = 8;

// Slow sensors are only polled when due and after enough fast polls have run.
const unsigned long sensorIntervalMs[SENSOR_COUNT] = {
  0,    // HV current: fast loop
  0,    // HV voltage: fast loop
  2000, // Coolant temp
  5000, // HV temps (multi-frame)
  2000, // SOC
  5000  // Fan mode
};

const unsigned long sensorTimeoutMs[SENSOR_COUNT] = {
  120, // HV current (multi-frame, value is in FF but we consume CF)
  300, // HV voltage (multi-frame)
  80,  // Coolant temp
  300, // HV temps (multi-frame)
  80,  // SOC
  120  // Fan mode
};
unsigned long sensorNextDueMs[SENSOR_COUNT] = {0};
uint8_t nextFastSensorIndex = 0;
uint8_t nextSlowSensorIndex = 0;
uint8_t fastPollsSinceSlow = 0;

const bool POLL_DIAG = false;
const unsigned long POLL_DIAG_GAP_MS = 100;
unsigned long pollDiagLastEventMs = 0;
unsigned long pollDiagLastRxMs = 0;

const char* sensorName(uint8_t sensor) {
    switch (sensor) {
        case SENSOR_HV_CURRENT: return "hv_current";
        case SENSOR_HV_VOLTAGE: return "hv_voltage";
        case SENSOR_ECT: return "coolant";
        case SENSOR_HV_TEMPS: return "hv_temps";
        case SENSOR_SOC: return "soc";
        case SENSOR_HV_FAN_MODE: return "fan_mode";
        default: return "unknown";
    }
}

void pollDiagMark(const char* tag, unsigned long now) {
    if (!POLL_DIAG) return;
    if (pollDiagLastEventMs != 0 && now - pollDiagLastEventMs >= POLL_DIAG_GAP_MS) {
        Serial.printf("[POLL %lu] GAP tag=%s dt=%lu waiting=%u sensor=%s req_elapsed=%lu since_last_rx=%lu\n",
                      now, tag, now - pollDiagLastEventMs, waiting ? 1 : 0,
                      sensorName(currentPollSensor),
                      waiting ? now - requestTimeout : 0,
                      pollDiagLastRxMs ? now - pollDiagLastRxMs : 0);
    }
    pollDiagLastEventMs = now;
}

void pollDiagFrame(const char* tag, unsigned long now, const CAN_FRAME& frame) {
    if (!POLL_DIAG) return;
    pollDiagMark(tag, now);
    pollDiagLastRxMs = now;
    Serial.printf("[POLL %lu] %s sensor=%s id=0x%03X len=%u data=",
                  now, tag, sensorName(currentPollSensor), frame.id, frame.length);
    for (uint8_t i = 0; i < frame.length; i++) {
        Serial.printf("%02X", frame.data.byte[i]);
        if (i + 1 < frame.length) Serial.print(' ');
    }
    Serial.println();
}

// g_sensors indexes
enum {
  IDX_RPM = 0,
  IDX_HV_CURRENT = 1,
  IDX_HV_VOLTAGE = 2,
  IDX_ECT = 3,
  IDX_HV_INTAKE_C = 4,
  IDX_HV_TB1_C = 5,
  IDX_HV_TB2_C = 6,
  IDX_HV_TB3_C = 7,
  IDX_BFS = 11,
  IDX_DASH_BRIGHT = 12,
  IDX_CAR_DIM = 13,
  IDX_DISPLAY_OFF = 14,
  IDX_MODE_EV = 15,
  IDX_MODE_ECO = 16,
  IDX_MODE_PWR = 17
};

float pollSensorValueForDiag(uint8_t sensor) {
    switch (sensor) {
        case SENSOR_HV_CURRENT: return g_sensors[IDX_HV_CURRENT];
        case SENSOR_HV_VOLTAGE: return g_sensors[IDX_HV_VOLTAGE];
        case SENSOR_ECT: return g_sensors[IDX_ECT];
        case SENSOR_HV_TEMPS: return g_sensors[IDX_HV_TB1_C];
        case SENSOR_SOC: return g_sensors[8];
        case SENSOR_HV_FAN_MODE: return g_sensors[IDX_BFS];
        default: return 0.0f;
    }
}

inline void completeCurrentSensor(unsigned long now) {
    if (currentPollSensor != SENSOR_NONE && currentPollSensor < SENSOR_COUNT) {
        pollDiagMark("DONE", now);
        if (POLL_DIAG) {
            Serial.printf("[POLL %lu] DONE sensor=%s elapsed=%lu value=%.2f\n",
                          now, sensorName(currentPollSensor), now - requestTimeout,
                          (double)pollSensorValueForDiag(currentPollSensor));
        }
        if (sensorIntervalMs[currentPollSensor] > 0) {
            sensorNextDueMs[currentPollSensor] = now + sensorIntervalMs[currentPollSensor];
            if (POLL_DIAG) {
                Serial.printf("[POLL %lu] NEXT_SLOW sensor=%s due_in=%lu\n",
                              now, sensorName(currentPollSensor), sensorIntervalMs[currentPollSensor]);
            }
        }
    }
    currentPollSensor = SENSOR_NONE;
    waiting = false;
}

inline void timeoutCurrentSensor(unsigned long now) {
    if (currentPollSensor != SENSOR_NONE && currentPollSensor < SENSOR_COUNT) {
        pollDiagMark("TIMEOUT", now);
        if (POLL_DIAG) {
            Serial.printf("[POLL %lu] TIMEOUT sensor=%s elapsed=%lu limit=%lu since_last_rx=%lu\n",
                          now, sensorName(currentPollSensor), now - requestTimeout,
                          sensorTimeoutMs[currentPollSensor],
                          pollDiagLastRxMs ? now - pollDiagLastRxMs : 0);
        }
        // On timeout, slow sensors wait for their next slow slot. Fast sensors
        // stay eligible so current/voltage recover immediately.
        if (sensorIntervalMs[currentPollSensor] > 0) {
            sensorNextDueMs[currentPollSensor] = now + sensorIntervalMs[currentPollSensor];
            if (POLL_DIAG) {
                Serial.printf("[POLL %lu] NEXT_SLOW_AFTER_TIMEOUT sensor=%s due_in=%lu\n",
                              now, sensorName(currentPollSensor), sensorIntervalMs[currentPollSensor]);
            }
        }
    }
    currentPollSensor = SENSOR_NONE;
    waiting = false;
}

int8_t pickNextDueSensor(unsigned long now) {
    const uint8_t slowCount = sizeof(slowSensors) / sizeof(slowSensors[0]);
    if (fastPollsSinceSlow >= FAST_POLLS_BETWEEN_SLOW_POLLS) {
        pollDiagMark("SLOW_SLOT", now);
        if (POLL_DIAG) {
            Serial.printf("[POLL %lu] SLOW_SLOT fast_polls=%u\n", now, fastPollsSinceSlow);
        }
        for (uint8_t i = 0; i < slowCount; i++) {
            uint8_t idx = (nextSlowSensorIndex + i) % slowCount;
            uint8_t sensor = slowSensors[idx];
            if (now >= sensorNextDueMs[sensor]) {
                nextSlowSensorIndex = (idx + 1) % slowCount;
                fastPollsSinceSlow = 0;
                if (POLL_DIAG) {
                    Serial.printf("[POLL %lu] PICK_SLOW sensor=%s overdue=%lu\n",
                                  now, sensorName(sensor), now - sensorNextDueMs[sensor]);
                }
                return sensor;
            }
        }
        fastPollsSinceSlow = 0;
        if (POLL_DIAG) {
            Serial.printf("[POLL %lu] SLOW_NONE_DUE\n", now);
        }
    }

    const uint8_t fastCount = sizeof(fastSensors) / sizeof(fastSensors[0]);
    uint8_t sensor = fastSensors[nextFastSensorIndex];
    nextFastSensorIndex = (nextFastSensorIndex + 1) % fastCount;
    if (fastPollsSinceSlow < 255) fastPollsSinceSlow++;
    pollDiagMark("PICK_FAST", now);
    if (POLL_DIAG) {
        Serial.printf("[POLL %lu] PICK_FAST sensor=%s fast_polls=%u\n",
                      now, sensorName(sensor), fastPollsSinceSlow);
    }
    return sensor;
}

static inline uint8_t xor_checksum(const uint8_t* p, size_t n) {
  uint8_t x = 0;
  for (size_t i = 0; i < n; ++i) x ^= p[i];
  return x;
}

#pragma pack(push,1)
// All primary values as IEEE-754 float (little-endian on ESP32)
struct PayloadF {
  uint8_t seq;          // increments each packet
  float   rpm;          // from g_sensors[IDX_RPM]
  float   hv_current_A; // can be negative
  float   hv_voltage_V;
  float   ect_C;
  float   hv_intake_C;
  float   tb1_C;
  float   tb2_C;
  float   tb3_C;
  float   soc_pct;
  int8_t  ebar;         // already small int
  uint8_t est;          // state_energy_drain
};
#pragma pack(pop)

static uint8_t tx_seq = 0;

void initDisplayUart() {
  DISP.begin(UART_BAUD, SERIAL_8N1, UART2_RX_PIN, UART2_TX_PIN);
}

static inline void wr_f32(uint8_t *dst, float v) { memcpy(dst, &v, 4); }

void sendSensorsFloat() {
    const uint8_t n = 43;                 // payload length (bytes)
    const int need = 1 + 1 + n + 1;       // [0xAA][len][payload][csum]
    if (DISP.availableForWrite() < need) return;

    uint8_t buf[1 + 1 + 43 + 1];          // <-- or: uint8_t buf[1 + 1 + n + 1];
    size_t o = 0;

    buf[o++] = 0xAA;
    buf[o++] = n;

    buf[o++] = tx_seq++;

    wr_f32(&buf[o], g_sensors[IDX_RPM]);         o += 4;
    wr_f32(&buf[o], g_sensors[IDX_HV_CURRENT]);  o += 4;
    wr_f32(&buf[o], g_sensors[IDX_HV_VOLTAGE]);  o += 4;
    wr_f32(&buf[o], g_sensors[IDX_ECT]);         o += 4;
    wr_f32(&buf[o], g_sensors[IDX_HV_INTAKE_C]); o += 4;
    wr_f32(&buf[o], g_sensors[IDX_HV_TB1_C]);    o += 4;
    wr_f32(&buf[o], g_sensors[IDX_HV_TB2_C]);    o += 4;
    wr_f32(&buf[o], g_sensors[IDX_HV_TB3_C]);    o += 4;
    wr_f32(&buf[o], g_sensors[8]);               o += 4;  // soc_pct

    buf[o++] = (int8_t)g_sensors[9];             // ebar
    buf[o++] = (uint8_t)g_sensors[10];           // est

    // NEW: fan speed + override flag (bytes 39, 40)
    buf[o++] = (uint8_t)g_sensors[IDX_BFS];      // fan speed 0..6
    buf[o++] = fanOverrideEnable ? 1 : 0;     // 0/1  (make sure this is a bool var)

    // car dim signal 
    buf[o++] = g_sensors[IDX_CAR_DIM] ? 1 : 0;

    // add display off flag
    buf[o++] = g_sensors[IDX_DISPLAY_OFF];

    uint8_t csum = xor_checksum(&buf[2], n);     // XOR over payload only
    buf[o++] = csum;


    // (Optional safety during bring-up)
    if (o != (size_t)(1 + 1 + n + 1)) { Serial.printf("PACK LEN BUG: o=%u exp=%u\n", (unsigned)o, 1+1+n+1); }

    DISP.write(buf, o);
}



////////////////////////////////////////////////////////////setup//////////////////////////////////////////////////////////
void setup() {
    Serial.begin(115200);
    Serial.println("------------------------");
    Serial.println("    MrDIY CAN SHIELD");
    Serial.println("------------------------");
    Serial.println(" CAN...............INIT");

    CAN0.setCANPins(GPIO_NUM_4, GPIO_NUM_5);
    CAN0.begin(500000); // 500Kbps

    // CAN0.watchFor();

    CAN0.watchFor(0x1C4); // engine RPM
    CAN0.watchFor(0x247); // energy bar + state_energy_drain
    CAN0.watchFor(0x620); // dashboard brightness + dim state
    CAN0.watchFor(0x7EA); // multi-frame responses go here
    CAN0.watchFor(0x610); // dimmer knob signal
    CAN0.watchFor(0x49B); // drive mode status
    CAN0.watchFor(0x58E); // steering wheel directional/enter/back buttons
    CAN0.watchFor(0x758); // body ECU positive responses (window/wireless buzzer ACKs)

    Serial.println(" CAN............500Kbps");

    initDisplayUart();

    // new esp-now stuff
    WiFi.mode(WIFI_STA);
    esp_now_init();
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, PEER_MAC, 6);
    peer.channel = 0; // use current channel
    peer.encrypt = false;
    esp_now_add_peer(&peer);
    Serial.println(" ESP-NOW.............INIT");

    const unsigned long now = millis();
    for (uint8_t s = 0; s < SENSOR_COUNT; s++) {
        sensorNextDueMs[s] = now;
    }
}

////////////////////////////////////////////////////////////main loop//////////////////////////////////////////////////////////
void loop() {
    CAN_FRAME can_message;
    unsigned long currentTime = millis();
    processSteeringControlState(currentTime);
    const bool windowBusy = isWindowMotionBusy();
    static bool lastWindowBusy = false;
    static unsigned long lastWindowWaitDiagMs = 0;
    static unsigned long lastWaitingDiagMs = 0;

    if (POLL_DIAG && windowBusy != lastWindowBusy) {
        Serial.printf("[POLL %lu] WINDOW_BUSY %s waiting=%u sensor=%s\n",
                      currentTime, windowBusy ? "ON" : "OFF", waiting ? 1 : 0,
                      sensorName(currentPollSensor));
        lastWindowBusy = windowBusy;
    }

    if (POLL_DIAG && windowBusy && waiting && currentTime - lastWindowWaitDiagMs >= 250) {
        Serial.printf("[POLL %lu] WAIT_PAUSED_BY_WINDOW sensor=%s elapsed=%lu\n",
                      currentTime, sensorName(currentPollSensor), currentTime - requestTimeout);
        lastWindowWaitDiagMs = currentTime;
    }

    if (POLL_DIAG && !windowBusy && waiting && currentTime - lastWaitingDiagMs >= POLL_DIAG_GAP_MS) {
        pollDiagMark("WAITING", currentTime);
        Serial.printf("[POLL %lu] WAITING sensor=%s elapsed=%lu limit=%lu since_last_rx=%lu\n",
                      currentTime, sensorName(currentPollSensor),
                      currentTime - requestTimeout,
                      currentPollSensor < SENSOR_COUNT ? sensorTimeoutMs[currentPollSensor] : 0,
                      pollDiagLastRxMs ? currentTime - pollDiagLastRxMs : 0);
        lastWaitingDiagMs = currentTime;
    }

    // STEP 1: PID scheduler
    if (!windowBusy && !waiting) {
        int8_t nextSensor = pickNextDueSensor(currentTime);
        if (nextSensor >= 0) {
            currentPollSensor = (uint8_t)nextSensor;
            if (POLL_DIAG) {
                pollDiagMark("REQ", currentTime);
                Serial.printf("[POLL %lu] REQ sensor=%s timeout=%lu\n",
                              currentTime, sensorName(currentPollSensor),
                              sensorTimeoutMs[currentPollSensor]);
            }
            sendSensorRequest(currentPollSensor);
            waiting = true;
            requestTimeout = currentTime;
        }
    }

    // STEP 2: in-flight timeout
    if (!windowBusy && waiting && currentPollSensor < SENSOR_COUNT && currentTime - requestTimeout >= sensorTimeoutMs[currentPollSensor]) {
        timeoutCurrentSensor(currentTime);
    }

    // STEP 3: Process CAN messages
    while (CAN0.read(can_message)) {

        // force battery fan on
        // sendCANFrame(0x7E2, {0x06,0x30,0x81,0x06,0x06,6,0x00,0x00});


        switch (can_message.id) {

            // Engine RPM (non-polled)
            case 0x1C4:
                g_sensors[0] = Process_Endian(can_message.data.byte[0], can_message.data.byte[1]);
                break;

            // energy bar (non-polled)
            case 0x247: { // BO_ 583 Display_1
                const uint8_t* d = can_message.data.byte;

                // SG_ BAR_ENERGY : 15|8@0-   -> signed 8-bit at byte 1
                int8_t bar_energy = (int8_t)d[1];

                // SG_ STATE_ENERGYDRAIN : 3|4@0+ -> bits 3..0 of byte 0
                uint8_t state_energy_drain = d[0] & 0x0F;

                // TODO: store them wherever you like
                // e.g., g_sensors[8] = bar_energy;
                //       g_sensors[9] = state_energy_drain;

                g_sensors[9] = bar_energy;
                g_sensors[10] = state_energy_drain;

                // (optional) quick print
                // Serial.print("BAR_ENERGY="); Serial.print(bar_energy);
                // Serial.print("  STATE_ENERGYDRAIN="); Serial.println(state_energy_drain);
                break;
            }

            // dashboard brightness / dim state
            case 0x620: {
                const uint8_t *d = can_message.data.byte;

                uint16_t als_raw = (uint16_t(d[2]) << 8) | d[3];   // D3<<8 | D4
                bool car_dim_active = (d[4] & 0x40) != 0;          // D5 bit6

                // Track observed range (or set fixed numbers you like)
                static uint16_t als_min = 80;    // darkest seen
                static uint16_t als_max = 600;   // brightest seen
                if (als_raw < als_min) als_min = als_raw;
                if (als_raw > als_max) als_max = als_raw;

                // ---- INVERTED map: dark -> low %, bright -> high % ----
                uint8_t ui_brightness_pct = 50;  // default
                if (als_max > als_min) {
                    uint16_t span = als_max - als_min;
                    uint16_t pos  = (als_raw <= als_min) ? 0 : (als_raw - als_min > span ? span : als_raw - als_min);
                    // invert here: 0..span -> 100..0
                    int pct = (int)((span - pos) * 100L / span);

                    // Optional floors so it's never too dim (different at night/day)
                    const uint8_t floor_day = 25;    // min brightness with lights off
                    const uint8_t floor_night = 5;   // min brightness with lights on
                    const uint8_t floor_pct = car_dim_active ? floor_night : floor_day;
                    if (pct < floor_pct) pct = floor_pct;

                    // Optional: mild gamma to match eye response (comment out if you want pure linear)
                    // float g = 1.3f; pct = (int)(powf(pct/100.0f, g)*100.0f + 0.5f);

                    ui_brightness_pct = (uint8_t)pct;
                }

                // store/send as before
                g_sensors[IDX_DASH_BRIGHT] = (float)ui_brightness_pct;   // percent for your display
                g_sensors[IDX_CAR_DIM] = car_dim_active ? 1.0f : 0.0f;

                // Serial.printf("ALS=%u -> %u%%  DIM=%d\n", als_raw, ui_brightness_pct, car_dim_active);
                break;
            }

            // dimmer knob signal, only used to tell when its all the way down
            case 0x610: {
                const bool dimmer_down = (can_message.data.byte[3] == 0x00);
                // Serial.println(dimmer_down ? "Dimmer Down" : "Dimmer Up");
                g_sensors[IDX_DISPLAY_OFF] = dimmer_down ? 1.0f : 0.0f;
                break;
            }

            case 0x49B: {
            // Guard length: we need at least 4 bytes (D1..D4)
            // print entire message for debugging
            // Serial.print("49B: ");
            // for (int i = 0; i < can_message.length; i++) {
            //   Serial.print(can_message.data.byte[i], HEX);
            //   Serial.print(" ");
            // }
            // Serial.println();
                if (can_message.length >= 4) {
                    uint8_t flags = can_message.data.byte[4];  // D4 in your CSV

                    bool ev_on  = (flags & 0x02) != 0;  // bit1
                    bool pwr_on = (flags & 0x04) != 0;  // bit2
                    bool eco_on = (flags & 0x08) != 0;  // bit3

                    // Optional: enforce mutual exclusivity ECO vs PWR (defensive)
                    if (eco_on && pwr_on) {
                    // If this ever happens due to noise, pick one policy:
                    // e.g., clear both or prefer the previous state.
                    eco_on = pwr_on = false;
                    }

                    // Store as 0/1 in g_sensors (or keep as bools if you prefer)
                    g_sensors[IDX_MODE_EV]  = ev_on  ? 1.0f : 0.0f;
                    g_sensors[IDX_MODE_ECO] = eco_on ? 1.0f : 0.0f;
                    g_sensors[IDX_MODE_PWR] = pwr_on ? 1.0f : 0.0f;

                    // Optional quick print
                    // Serial.printf("Modes: EV=%d ECO=%d PWR=%d (flags=0x%02X)\n", ev_on, eco_on, pwr_on, flags);
                }
                break;
            }

            case 0x58E: {
                // Steering button code is in D6.
                uint8_t code = can_message.data.byte[5];
                handleSteeringButton(code, currentTime);
                break;
            }

            case 0x758: {
                handleBodyAckFrame(can_message);
                break;
            }




            // Polled sensor responses
            case 0x7EA: {

                // Debug dump of 7EA frames
                // Serial.print("7EA: ");
                // for (int i = 0; i < can_message.length; i++) {
                //   Serial.print(can_message.data.byte[i], HEX);
                //   Serial.print(" ");
                // }
                // Serial.println();

	                if (waiting) {
	                    const uint8_t* b = can_message.data.byte;
	                    const uint8_t  pciType = b[0] & 0xF0; // 0x00=SF, 0x10=FF, 0x20=CF, 0x30=FC
	                    const uint8_t  seq     = b[0] & 0x0F; // CF sequence (1..15)

                        // ISO-TP timing matters: answer first frames with FC before
                        // any Serial diagnostics can block the loop.
                        bool sentImmediateFc = false;
                        if (pciType == 0x10 && can_message.length >= 4) {
                            const bool needsFc =
                                (currentPollSensor == SENSOR_HV_CURRENT && b[2] == 0x61 && b[3] == 0x98) ||
                                (currentPollSensor == SENSOR_HV_VOLTAGE && b[2] == 0x61 && b[3] == 0x74) ||
                                (currentPollSensor == SENSOR_HV_TEMPS && b[2] == 0x61 && b[3] == 0x87);
                            if (needsFc) {
                                sendFlowControl(0x7E2, 0x00, 0x00);
                                sentImmediateFc = true;
                            }
                        }

	                    pollDiagFrame("RX", currentTime, can_message);
                        if (sentImmediateFc && POLL_DIAG) {
                            Serial.printf("[POLL %lu] FC_IMMEDIATE sensor=%s target=0x7E2 stmin=0\n",
                                          currentTime, sensorName(currentPollSensor));
                        }

                    switch (currentPollSensor) {
	                        // ---------------------- Battery Current (21 98) ----------------------
	                        case 0: {
	                            bool decoded = false;

                            // SF layout (if ever used): b[1]=0x61, b[2]=0x98, A=b[3], B=b[4]
                            // if (pciType == 0x00 /*SF*/ && b[1] == 0x61 && b[2] == 0x98 && can_message.length >= 5) {
                            //     g_sensors[1] = ((b[3] * 256 + b[4]) / 100.0f) - 327.7f;
                            //     decoded = true;
                            // }

	                            // FF layout (your logs show FF for 61 98): A=b[4], B=b[5]
		                            if (pciType == 0x10 /*FF*/ && b[2] == 0x61 && b[3] == 0x98 && can_message.length >= 6) {
		                                g_sensors[1] = ((b[4] * 256 + b[5]) / 100.0f) - 327.7f;
		                                if (POLL_DIAG) {
		                                    Serial.printf("[POLL %lu] DECODE sensor=%s amps=%.2f\n",
		                                                  currentTime, sensorName(currentPollSensor),
		                                                  (double)g_sensors[IDX_HV_CURRENT]);
		                                }
		                                // Value is in the FF, but finish the ISO-TP exchange by
	                                    // waiting for CF#1 before starting another request.
		                            }

                                if (pciType == 0x20 /*CF*/ && seq == 0x01) {
                                    decoded = true;
                                }

	                            if (decoded) {
	                                completeCurrentSensor(currentTime);
                            }
                            break;
                        }

                        // ---------------------- Battery Voltage (21 74) ----------------------
                        case 1: {
		                            if (pciType == 0x10 /*FF*/ && b[2] == 0x61 && b[3] == 0x74) {
		                                // Be loud so we know this ran:
		                                // Serial.println("FF 61 74 seen -> sending FC to 0x7EA and 0x7E2 (BS=0, STmin=5ms)");
		                                // Send FC back to the ECU so it sends the consecutive frame.
		                                // do NOT advance; wait for CF#1 (seq==1), which carries F/G
	                            }


                            // CF#1 carries F,G at b[2],b[3] → Voltage=(F*256+G)/2
	                            if (pciType == 0x20 /*CF*/ && seq == 0x01 && can_message.length >= 4) {
	                                uint16_t raw = (uint16_t(b[2]) << 8) | b[3]; // F,G
	                                g_sensors[2] = raw / 2.0f;                    // volts
                                    if (POLL_DIAG) {
                                        Serial.printf("[POLL %lu] DECODE sensor=%s volts=%.1f\n",
                                                      currentTime, sensorName(currentPollSensor),
                                                      (double)g_sensors[IDX_HV_VOLTAGE]);
                                    }
	                                completeCurrentSensor(currentTime);
	                            }
                            break;
                        }

                        // ---------------------- Coolant Temp (Mode 01 PID 05) ----------------------
                        case 2: {
                            bool decoded = false;

                            // Proper Mode 01 SF: b[1]=0x41, b[2]=0x05, A=b[3]
	                            if (pciType == 0x00 /*SF*/ && b[1] == 0x41 && b[2] == 0x05 && can_message.length >= 4) {
	                                g_sensors[3] = b[3] - 40.0f;
	                                decoded = true;
                                    if (POLL_DIAG) {
                                        Serial.printf("[POLL %lu] DECODE sensor=%s c=%.1f\n",
                                                      currentTime, sensorName(currentPollSensor),
                                                      (double)g_sensors[IDX_ECT]);
                                    }
	                            }

                            if (decoded) {
                                completeCurrentSensor(currentTime);
                            }
                            break;
                        }

                        case 3: { // 21 87 — HV battery temps & intake temp
                            const uint8_t* b = can_message.data.byte;
                            const uint8_t  pciType = b[0] & 0xF0; // 0x10=FF, 0x20=CF
                            const uint8_t  seq     = b[0] & 0x0F;

                            auto word_to_C = [](uint16_t w)->float {
                                return (w * 255.9f / 65535.0f) - 50.0f;  // Celsius per Torque equation
                            };

                            // FF: 61 87, payload: A=b[4],B=b[5],C=b[6],D=b[7]
		                            if (pciType == 0x10 && b[2] == 0x61 && b[3] == 0x87) {
		                                // Send Flow Control to the ECU so we get CF#1 with E..H

	                                uint16_t AB = (uint16_t(b[4]) << 8) | b[5]; // Intake
                                uint16_t CD = (uint16_t(b[6]) << 8) | b[7]; // TB1
	                                g_sensors[IDX_HV_INTAKE_C] = word_to_C(AB);
	                                g_sensors[IDX_HV_TB1_C]    = word_to_C(CD);
                                    if (POLL_DIAG) {
                                        Serial.printf("[POLL %lu] DECODE_PART sensor=%s intake=%.1f tb1=%.1f\n",
                                                      currentTime, sensorName(currentPollSensor),
                                                      (double)g_sensors[IDX_HV_INTAKE_C],
                                                      (double)g_sensors[IDX_HV_TB1_C]);
                                    }
	                                // Do not advance yet; finish TB2/TB3 from CF#1
	                            }

                            // CF#1: seq==1, payload: E=b[1],F=b[2],G=b[3],H=b[4],I=b[5]...
                            if (pciType == 0x20 && seq == 0x01 && can_message.length >= 5) {
                                uint16_t EF = (uint16_t(b[1]) << 8) | b[2]; // TB2 uses E,F
	                                uint16_t GH = (uint16_t(b[3]) << 8) | b[4]; // TB3 uses G,H
	                                g_sensors[6] = (EF * 255.9f / 65535.0f) - 50.0f; // TB2 °C
	                                g_sensors[7] = (GH * 255.9f / 65535.0f) - 50.0f; // TB3 °C
                                    if (POLL_DIAG) {
                                        Serial.printf("[POLL %lu] DECODE sensor=%s tb2=%.1f tb3=%.1f\n",
                                                      currentTime, sensorName(currentPollSensor),
                                                      (double)g_sensors[IDX_HV_TB2_C],
                                                      (double)g_sensors[IDX_HV_TB3_C]);
                                    }

	                                completeCurrentSensor(currentTime);
	                            }

                            break;
                        }

                        case 4: { // SOC - 01 5B
                            bool decoded = false;
                            const uint8_t* b = can_message.data.byte;
                            uint8_t pciType = b[0] & 0xF0; // 0x00 = SF

                            // Mode 01 SF reply: b[1]=0x41, b[2]=0x5B, A=b[3]
	                            if (pciType == 0x00 && b[1] == 0x41 && b[2] == 0x5B && can_message.length >= 4) {
	                                float soc = (b[3] * 20.0f) / 51.0f; // percent
	                                g_sensors[8] = soc;                 // pick any free slot; e.g., index 8
	                                decoded = true;
                                    if (POLL_DIAG) {
                                        Serial.printf("[POLL %lu] DECODE sensor=%s pct=%.1f\n",
                                                      currentTime, sensorName(currentPollSensor),
                                                      (double)g_sensors[8]);
                                    }
	                            }
                            if (decoded) {
                                completeCurrentSensor(currentTime);
                            }
                            break;
                        }
                        
                        // ---------------------- HV battery fan mode (21 9B) ----------------------
                        case 5: {
                            bool decoded = false;
                            uint8_t mode = 0xFF;  // invalid placeholder

                            // We’re in 0x7EA and waiting==true already
                            // pciType & seq are computed above

                            //print this entire frame for debugging
                            // for(int i=0; i<can_message.length; i++) {
                            //     Serial.print(can_message.data.byte[i], HEX);
                            //     Serial.print(" ");
                            // }
                            // Serial.println();
                            

                            // Single Frame: [00 len][61][9B][A][B]...
                            if (pciType == 0x00 /*SF*/ && b[1] == 0x61 && b[2] == 0x9B) {
                                if (can_message.length >= 5) {          // need A and B present
                                    mode = b[4];                        // B = second byte after 61 9B
                                    decoded = true;
                                }
                            }

                            // First Frame: [10 xx][61][9B][A][B]...
                            if (pciType == 0x10 /*FF*/ && b[2] == 0x61 && b[3] == 0x9B) {
                                if (can_message.length >= 6) {          // A at b[4], B at b[5]
                                    mode = b[5];
                                    decoded = true;
                                    // No Flow Control needed: we already have byte B in the FF
                                }
                            }

                            // (Ignore CFs — we don’t need them for byte B)

	                            if (decoded) {
	                                if (mode <= 6) {
	                                    g_sensors[11] = mode;               // pick free slot for BFS
	                                } else {
	                                    g_sensors[11] = 0;                  // clamp/guard if odd value
	                                }
                                    if (POLL_DIAG) {
                                        Serial.printf("[POLL %lu] DECODE sensor=%s mode=%u\n",
                                                      currentTime, sensorName(currentPollSensor),
                                                      (unsigned)g_sensors[IDX_BFS]);
                                    }
	                                completeCurrentSensor(currentTime);
	                            }
	                            break;
	                        }



                    }
	                } else if (POLL_DIAG) {
                        pollDiagFrame("STRAY_RX_NOT_WAITING", currentTime, can_message);
                    }
	                break;
	            }

            default:
                break;
        }
    } // end CAN read drain loop

    // fan override every 2 seconds if enabled
    static unsigned long lastFanOverrideTime = 0;

    // if any battery temp is over 37c, enable fan override
    if(g_sensors[IDX_HV_TB1_C] > 37.0 || g_sensors[IDX_HV_TB2_C] > 37.0 || g_sensors[IDX_HV_TB3_C] > 37.0) {
        fanOverrideEnable = 1;
    } else {
        fanOverrideEnable = 0;
    }

    if (currentTime - lastFanOverrideTime >= 2000) {
        if(fanOverrideEnable == 1) {
            // force battery fan on
            sendCANFrame(0x7E2, {0x06,0x30,0x81,0x06,0x06,6,0x00,0x00});
        }
        lastFanOverrideTime = currentTime;
    }



    // STEP 4: Serial output
    static unsigned long lastPrintTime = 0;
    if (currentTime - lastPrintTime >= 30) {
        // Serial.print(F("RPM: "));       Serial.print(g_sensors[IDX_RPM]);            Serial.print(' ');
        // Serial.print(F("Bat I: "));     Serial.print(g_sensors[IDX_HV_CURRENT], 2);  Serial.print(F("A "));
        // Serial.print(F("Bat V: "));     Serial.print(g_sensors[IDX_HV_VOLTAGE], 1);  Serial.print(F("V "));
        // Serial.print(F("Coolant: "));   Serial.print(g_sensors[IDX_ECT], 1);         Serial.print(F("C "));
        // Serial.print(F("HV Intake: ")); Serial.print(g_sensors[IDX_HV_INTAKE_C], 1); Serial.print(F("C "));
        // Serial.print(F("TB1: "));       Serial.print(g_sensors[IDX_HV_TB1_C], 1);    Serial.print(F("C "));
        // Serial.print(F("TB2: "));       Serial.print(g_sensors[IDX_HV_TB2_C], 1);    Serial.print(F("C "));
        // Serial.print(F("TB3: "));       Serial.print(g_sensors[IDX_HV_TB3_C], 1);    Serial.print(F("C "));
        // Serial.print(F("SOC: "));       Serial.print(g_sensors[8], 1);               Serial.print(F("% "));
        // Serial.print(F("Ebar: "));      Serial.print((int)g_sensors[9]);             Serial.print(F("  "));
        // Serial.print(F("ES: "));        Serial.print((int)g_sensors[10]);            Serial.print(F("  "));
        // Serial.print(F("BFS: "));     Serial.print((int)g_sensors[11]);            Serial.print(F("  "));
        // Serial.print(F("Dash Bright: ")); Serial.print((int)g_sensors[IDX_DASH_BRIGHT]); Serial.print(F("%  "));
        // Serial.print(F("Car Dim: "));  Serial.print((int)g_sensors[IDX_CAR_DIM]);    Serial.print(F("  "));
        // Serial.println();

        // sendCANFrame(0x7E2, {0x06,0x30,0x81,0x06,0x06,6,0x00,0x00});

        sendSensorsFloat();
        lastPrintTime = currentTime;
    }

    // STEP 5: ESP-NOW send
    uint8_t engine_on = (g_sensors[IDX_RPM] > 500.0f) ? 1 : 0;
    uint8_t car_dim = (uint8_t)g_sensors[IDX_CAR_DIM] ? 1 : 0;
    uint8_t ev_mode = int(g_sensors[IDX_MODE_EV]); // placeholder for future use
    uint8_t display_off = int(g_sensors[IDX_DISPLAY_OFF]); 

    uint8_t flags =
        (engine_on << 0) |
        (car_dim << 1) |
        (ev_mode << 2) |
        (display_off << 3);
    
    unsigned long now = millis();
    if (flags != last_flags || (now - last_send) >= 500) {
        esp_now_send(PEER_MAC, &flags, 1);
        last_flags = flags;
        last_send = now;

        // Serial.print("ESP-NOW send: ");
        // Serial.println(flags, BIN);
    }




}
