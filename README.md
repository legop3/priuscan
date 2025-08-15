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

### Addresses and calculations:

Engine RPM: 0x1C4 

Eco and PWR mode: 0x3BC
EV MODE: 0X49B

brake pedal but NOT break lights: 0x361
brake pedal position: 0x224

turn signals: 0x614
