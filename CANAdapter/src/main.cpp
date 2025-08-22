#include <Arduino.h>
#include <esp32_can.h> /* https://github.com/collin80/esp32_can */

#define SHIELD_LED_PIN 26

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

// Pre-allocated frame to avoid repeated memory allocation
CAN_FRAME txFrame;

void sendCANFrame(uint32_t canID, const uint8_t* data, uint8_t dataLength, bool extended = false, bool rtr = false) {
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
  sendCANFrame(req_id, fc, 8); // FC goes to the *responder* ID in our decoder (we'll use 0x7EA there)
}

// Optimized overload using stack array
void sendCANFrame(uint32_t canID, std::initializer_list<uint8_t> data, bool extended = false, bool rtr = false) {
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
    }
}

/////////////////////////////////////////////////////////////global variables//////////////////////////////////////////////////////////
// Timing control for polling
unsigned long lastPollTime = 0;
const unsigned long POLL_INTERVAL = 100; // Poll every 100ms

// Sensor polling stuff
volatile float g_sensors[20] = {0}; // All sensor values
uint8_t sensor_num = 0;             // Which sensor we're on (0,1,2,3,4...)
bool waiting = false;               // Are we waiting for a response?
unsigned long requestTimeout = 0;   // When we sent the last request
const unsigned long TIMEOUT_MS = 300; // a little more slack for multi-frame
int num_polling_sensors = 5; // number of sensors that get polled

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
};

// advance only after a successful decode
inline void advanceAfterDecode(unsigned long now) {
    sensor_num++;
    if (sensor_num >= num_polling_sensors) {
        sensor_num = 0;
        lastPollTime = now;
        waiting = false;
    } else {
        sendSensorRequest(sensor_num);
        requestTimeout = now;
    }
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

void sendSensorsFloat() {
  PayloadF pl{};
  pl.seq          = tx_seq++;
  pl.rpm          = g_sensors[IDX_RPM];
  pl.hv_current_A = g_sensors[IDX_HV_CURRENT];
  pl.hv_voltage_V = g_sensors[IDX_HV_VOLTAGE];
  pl.ect_C        = g_sensors[IDX_ECT];
  pl.hv_intake_C  = g_sensors[IDX_HV_INTAKE_C];
  pl.tb1_C        = g_sensors[IDX_HV_TB1_C];
  pl.tb2_C        = g_sensors[IDX_HV_TB2_C];
  pl.tb3_C        = g_sensors[IDX_HV_TB3_C];
  pl.soc_pct      = g_sensors[8];
  pl.ebar         = (int8_t)g_sensors[9];
  pl.est          = (uint8_t)g_sensors[10];

  const uint8_t* p = reinterpret_cast<const uint8_t*>(&pl);
  const uint8_t  n = (uint8_t)sizeof(PayloadF);
  const uint8_t  csum = xor_checksum(p, n);

  // Non-blocking guard: [start][len][payload][checksum]
  const int need = 1 + 1 + n + 1;
  if (DISP.availableForWrite() < need) return;

  DISP.write((uint8_t)0xAA);
  DISP.write(n);
  DISP.write(p, n);
  DISP.write(csum);

//   Serial.print("Sent to display");
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
    CAN0.watchFor();

    Serial.println(" CAN............500Kbps");

    // Pre-configure the polling frame to avoid repeated setup
    txFrame.extended = false;
    txFrame.rtr = false;

    initDisplayUart();
}

////////////////////////////////////////////////////////////main loop//////////////////////////////////////////////////////////
void loop() {
    CAN_FRAME can_message;
    unsigned long currentTime = millis();

    // STEP 1: Handle polling (independent of CAN messages)
    if (!waiting && currentTime - lastPollTime >= POLL_INTERVAL) {
        sendSensorRequest(sensor_num);
        waiting = true;
        requestTimeout = currentTime;
    }

    // STEP 2: Handle timeout
    if (waiting && currentTime - requestTimeout >= TIMEOUT_MS) {
        Serial.println("Timeout on sensor " + String(sensor_num));

        // Move to next sensor
        sensor_num++;
        if (sensor_num >= num_polling_sensors) {
            sensor_num = 0;
            lastPollTime = currentTime;
            waiting = false;
        } else {
            // Send next sensor request immediately
            sendSensorRequest(sensor_num);
            requestTimeout = currentTime;
        }
    }

    // STEP 3: Process CAN messages
    if (CAN0.read(can_message)) {
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

                    switch (sensor_num) {
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
                                decoded = true;
                                // We don't need CFs for current since A/B are in the FF already
                            }

                            if (decoded) {
                                advanceAfterDecode(currentTime);
                            }
                            break;
                        }

                        // ---------------------- Battery Voltage (21 74) ----------------------
                        case 1: {
                            if (pciType == 0x10 /*FF*/ && b[2] == 0x61 && b[3] == 0x74) {
                                // Be loud so we know this ran:
                                // Serial.println("FF 61 74 seen -> sending FC to 0x7EA and 0x7E2 (BS=0, STmin=5ms)");
                                // Send FC to responder (0x7EA) AND to request ID (0x7E2), some gateways want one or the other
                                sendFlowControl(0x7EA, 0x00, 0x05);  // BS=0 (all), STmin=5ms
                                sendFlowControl(0x7E2, 0x00, 0x05);  // belt & suspenders
                                // do NOT advance; wait for CF#1 (seq==1), which carries F/G
                            }


                            // CF#1 carries F,G at b[2],b[3] → Voltage=(F*256+G)/2
                            if (pciType == 0x20 /*CF*/ && seq == 0x01 && can_message.length >= 4) {
                                uint16_t raw = (uint16_t(b[2]) << 8) | b[3]; // F,G
                                g_sensors[2] = raw / 2.0f;                    // volts
                                advanceAfterDecode(currentTime);
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
                            }

                            if (decoded) {
                                advanceAfterDecode(currentTime);
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
                                // Send Flow Control to responder so we get CF#1 with E..H
                                sendFlowControl(0x7EA, 0x00, 0x05); // BS=0, STmin=5ms (robust)
                                sendFlowControl(0x7E2, 0x00, 0x05); // BS=0, STmin=5ms (robust)

                                uint16_t AB = (uint16_t(b[4]) << 8) | b[5]; // Intake
                                uint16_t CD = (uint16_t(b[6]) << 8) | b[7]; // TB1
                                g_sensors[IDX_HV_INTAKE_C] = word_to_C(AB);
                                g_sensors[IDX_HV_TB1_C]    = word_to_C(CD);
                                // Do not advance yet; finish TB2/TB3 from CF#1
                            }

                            // CF#1: seq==1, payload: E=b[1],F=b[2],G=b[3],H=b[4],I=b[5]...
                            if (pciType == 0x20 && seq == 0x01 && can_message.length >= 5) {
                                uint16_t EF = (uint16_t(b[1]) << 8) | b[2]; // TB2 uses E,F
                                uint16_t GH = (uint16_t(b[3]) << 8) | b[4]; // TB3 uses G,H
                                g_sensors[6] = (EF * 255.9f / 65535.0f) - 50.0f; // TB2 °C
                                g_sensors[7] = (GH * 255.9f / 65535.0f) - 50.0f; // TB3 °C

                                advanceAfterDecode(currentTime);
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
                            }
                            if (decoded) {
                                advanceAfterDecode(currentTime);
                            }
                            break;
                        }


                    }
                }
                break;
            }

            default:
                break;
        }
    }

    // STEP 4: Serial output
    static unsigned long lastPrintTime = 0;
    if (currentTime - lastPrintTime >= 100) {
        Serial.print("RPM: " + String(g_sensors[0]) + " ");
        Serial.print("Bat I: " + String(g_sensors[1], 2) + "A ");
        Serial.print("Bat V: " + String(g_sensors[2], 1) + "V ");
        Serial.print("Coolant: " + String(g_sensors[3], 1) + "C ");
        Serial.print("HV Intake: "); Serial.print(g_sensors[IDX_HV_INTAKE_C], 1); Serial.print("C ");
        Serial.print("TB1: "); Serial.print(g_sensors[IDX_HV_TB1_C], 1); Serial.print("C ");
        Serial.print("TB2: "); Serial.print(g_sensors[IDX_HV_TB2_C], 1); Serial.print("C ");
        Serial.print("TB3: "); Serial.print(g_sensors[IDX_HV_TB3_C], 1); Serial.print("C ");
        Serial.print("SOC: "); Serial.print(g_sensors[8], 1); Serial.print("% ");
        Serial.print("Ebar: "); Serial.print(g_sensors[9]); Serial.print("  ");
        Serial.print("ES: "); Serial.print(g_sensors[10]); Serial.print("  ");
        Serial.println();

        sendSensorsFloat();




        lastPrintTime = currentTime;
    }
}