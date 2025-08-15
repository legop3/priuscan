#include <Arduino.h>
#include <esp32_can.h> /* https://github.com/collin80/esp32_can */

#define  SHIELD_LED_PIN   26

// CAN_FRAME fan_speed_6;


uint16_t Process_Endian(uint8_t byte_msb, uint8_t byte_lsb) {
  return (byte_msb << 8) | byte_lsb;
}

void process_engine_rpm(uint8_t *byte_data)
{
  // uint16_t engine_rpm = (byte_0 << 8) | byte_1;
  // Serial.print("Engine RPM: ");  Serial.println(engine_rpm);

  uint16_t engine_rpm = Process_Endian(byte_data[0], byte_data[1]);
  Serial.print("Engine RPM: ");
  Serial.println(engine_rpm);


}



void setup()
{
  Serial.begin(115200);

  Serial.println("------------------------");
  Serial.println("    MrDIY CAN SHIELD");
  Serial.println("------------------------");

  Serial.println(" CAN...............INIT");
  CAN0.setCANPins(GPIO_NUM_4, GPIO_NUM_5); //config for shield v1.3+, see important note above!
  CAN0.begin(500000); // 500Kbps
  CAN0.watchFor();
  Serial.println(" CAN............500Kbps");

  // fan_frame_setup();

  // uint8_t fan_speed_6_data[8] = { 0x04, 0x30, 0x81, 0x06, 0x00, 0x00, 0x00, 0x00 };
  // fan_speed_6.id = 0x7E2;
  // fan_speed_6.length = 8;
  // // fan_speed_6.data.bytes = { 0x04, }
  // memcpy(fan_speed_6.data.bytes, fan_speed_6_data, 8);


}



void loop()
{

  CAN_FRAME fan_speed_6;
  fan_speed_6.id = 0x7E2;
  fan_speed_6.length = 8;
  fan_speed_6.extended = false;
  fan_speed_6.rtr = 0;
  fan_speed_6.data.byte[0] = 0x04;
  fan_speed_6.data.byte[1] = 0x30;
  fan_speed_6.data.byte[2] = 0x81;
  fan_speed_6.data.byte[3] = 0x06;
  fan_speed_6.data.byte[4] = 0x06;
  fan_speed_6.data.byte[5] = 0x00;
  fan_speed_6.data.byte[6] = 0x00;
  fan_speed_6.data.byte[7] = 0x00;

  CAN0.sendFrame(fan_speed_6);

  CAN_FRAME can_message;

  if (CAN0.read(can_message))
  {



    // CAN0.sendFrame(fan_speed_6);
    // Serial.print("CAN MSG: 0x");
    // Serial.print(can_message.id, HEX);
    // Serial.print(" [");
    // Serial.print(can_message.length, DEC);
    // Serial.print("] <");
    // for (int i = 0; i < can_message.length; i++)
    // {
    //   if (i != 0) Serial.print(":");
    //   Serial.print(can_message.data.byte[i], HEX);
    // }
    // Serial.println(">");

    switch (can_message.id)
    {
    case 0x1C4:
      // Serial.println(can_message.data.byte[1]);
      // process_engine_rpm(can_message.data.bytes);
      // Serial.print(can_message.data.byte[0], HEX);
      // Serial.print(":");
      // Serial.println(can_message.data.byte[1], HEX);

      process_engine_rpm(can_message.data.byte);
      /* code */
      break;
    
    default:
      break;
    }
  }
}