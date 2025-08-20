# ESP32 firmware for interfacing with a 2016 Prius V
This is a work in progress, it will use platformIO

## Notes

Things to read:
- KW from engine
- KW in / out from battery
- Braking mode (regen, mechanical, none)
- Battery percentage
- Battery fan speed
- Engine RPM
- MG temps
- Inverter temp
- Battery voltage
- Turn signals / hazards
- Headlights on / off for auto brightness
- Temps:
  - Battery temperature average
  - Battery intake air temperature
  - Intake air temperature
  - Engine coolant temperature
  - Cylinder tempuratures (?)

Things to control:
- Windows up/down/position?
- EV mode toggle
- Battery fan speed (6 levels)

touchtest.ino works for touchscreen

## Addresses and calculations:

Engine RPM: 0x1C4 

Eco and PWR mode: 0x3BC
EV MODE: 0X49B

brake pedal but NOT break lights: 0x361
brake pedal position: 0x224

turn signals: 0x614

0x621: something to do with door locking / unlocking!!

# 0x631
0x631: fuzzing this made the car go crazy, and made the windows move!!!

lock: 18 80 53 02 00 99 00 00
substitute 99: 
99 81 09 91 19 01 

gauge cluster: `18 80 df * * * * *`
  - double beep: `18 80 df 38 * * *`
  - warning: `18 80 df 1e 00 00 00 00`

631 testing grid:
1: lock doors (works with car off)
2: 

## requested PIDs

torque to can:
(bytes count from 0 for use in cpp)
byte A = byte 4
byte B = byte 5
byte C = byte 6
byte D = byte 7

battery current in amps: 
  - equation: (A * 256 + B) / 100 - 327.68
  - A = byte 4, B = byte 5
  - request: 0x7E2: 0x02 0x21 0x98 0x00 0x00 0x00 0x00 0x00 

battery voltage in v:
  - equation: (AE * 256 + AF) / 10
  

cpp notes:

send a CAN frame assembled manually:

  // CAN_FRAME fan_speed_6;
  // fan_speed_6.id = 0x7E2;
  // fan_speed_6.length = 8;
  // fan_speed_6.extended = false;
  // fan_speed_6.rtr = 0;
  // fan_speed_6.data.byte[0] = 0x04;
  // fan_speed_6.data.byte[1] = 0x30;
  // fan_speed_6.data.byte[2] = 0x81;
  // fan_speed_6.data.byte[3] = 0x06;
  // fan_speed_6.data.byte[4] = 0x06;
  // fan_speed_6.data.byte[5] = 0x00;
  // fan_speed_6.data.byte[6] = 0x00;
  // fan_speed_6.data.byte[7] = 0x00;
  // CAN0.sendFrame(fan_speed_6);
  // Serial.println("reauesting battery current");

