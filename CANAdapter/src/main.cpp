#include <Arduino.h>
#include <esp32_can.h> /* https://github.com/collin80/esp32_can */

#define SHIELD_LED_PIN 26

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

// Optimized overload using stack array
void sendCANFrame(uint32_t canID, std::initializer_list<uint8_t> data, bool extended = false, bool rtr = false) {
    uint8_t dataArray[8];
    uint8_t length = (data.size() > 8) ? 8 : data.size();
    
    // Use iterators for potentially faster access
    auto it = data.begin();
    for (uint8_t i = 0; i < length; ++i, ++it) {
        dataArray[i] = *it;
    }
    
    sendCANFrame(canID, dataArray, length, extended, rtr);
}

/////////////////////////////////////////////////////////////global variables//////////////////////////////////////////////////////////
volatile int g_engine_rpm = 0;
volatile float g_battery_current = 0.0000f;

// Timing control for polling
unsigned long lastPollTime = 0;
const unsigned long POLL_INTERVAL = 100; // Poll every 100ms instead of every message

///////////////////////////////////////////////decoder functions//////////////////////////////////////////////////////////
inline uint16_t process_engine_rpm(const uint8_t *byte_data) {
    uint16_t engine_rpm = Process_Endian(byte_data[0], byte_data[1]);
    // Serial.print("Engine RPM: ");
    // Serial.println(engine_rpm);
    return engine_rpm;
}

inline float process_battery_current(const uint8_t *byte_data) {
    // Assuming the battery current is in the first two bytes
    // int16_t current = Process_Endian(byte_data[0], byte_data[1]);
    // float battery_current = static_cast<float>(current) / 100.0f; // Adjust scaling as needed
    // Serial.print("Battery Current: ");
    // Serial.println(battery_current);
    float battery_current = ((byte_data[4] * 256 + byte_data[5]) / 100.0f) - 327.28f;
    // int battery_current = Process_Endian(byte_data[4], byte_data[5]) - 32728; // Adjust scaling as needed
// 
    return battery_current;
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
}

////////////////////////////////////////////////////////////main loop//////////////////////////////////////////////////////////
void loop() {
    CAN_FRAME can_message;
    
    if (CAN0.read(can_message)) {
        // Process received message first
        switch (can_message.id) {

            // engine RPM
            case 0x1C4:
                g_engine_rpm = process_engine_rpm(can_message.data.byte);
                break;
                
            case 0x7EA: {
                // Use block scope to limit variable lifetime
                uint8_t pid = can_message.data.byte[3];
                switch (pid) {

                    // battery current
                    case 0x98:
                        g_battery_current = process_battery_current(can_message.data.byte);



                        break;
                    default:
                        break;
                }
                break;
            }
            
            default:
                break;
        }
        
        // Rate-limited polling instead of polling on every received message
        unsigned long currentTime = millis();
        if (currentTime - lastPollTime >= POLL_INTERVAL) {


            // Request battery current with pre-allocated data
            static const uint8_t batteryCurrentRequest[] = {0x02, 0x21, 0x98, 0x00, 0x00, 0x00, 0x00, 0x00};
            sendCANFrame(0x7E2, batteryCurrentRequest, 8);



            lastPollTime = currentTime;
        }
        
        // Reduce serial output frequency for better performance
        static unsigned long lastPrintTime = 0;
        if (currentTime - lastPrintTime >= 100) { // Print every 100ms


            Serial.print("engine rpm: " + String(g_engine_rpm) + " RPM\t");
            Serial.print("battery current: " + String(g_battery_current, 5) + " A\t");
            Serial.print("\n");

            lastPrintTime = currentTime;
        }
    }
}