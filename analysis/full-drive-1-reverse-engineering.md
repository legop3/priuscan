# Full Drive 1 Reverse Engineering Report

## Scope
- Vehicle: 2016 Prius v Four, no Advanced Technology Package.
- Capture point: OBD-II DLC3 HS-CAN.
- Priority: cabin/body/display/HVAC/text/control signals over common drivetrain signals.
- This is a running report. Confidence labels are used where the passive log does not prove meaning.

## Inputs
- Main log: `CANAdapter/candumps/full drive 1.csv`
- Existing DBCs are used as coverage filters, not as the final output target.

## First-Pass Inventory
- Total frames parsed: 1672821
- Standard IDs observed: 115
- IDs already present in at least one existing DBC: 63
- IDs not present in the existing DBC set: 52

## Highest-Volume IDs Not In Existing DBCs
Timing values are microseconds. `Approx Hz` is derived from the median inter-frame gap.

| ID | Count | DLC | Median us | Approx Hz | Changing bytes | Byte ranges | Notes |
|---|---:|---|---:|---:|---|---|---|
| 0x020 | 133836 | 7 | 12111 | 82.6 |  | 20-20 00-00 07-07 01-01 00-00 00-00 4F-4F 00-00 | constant high-rate status |
| 0x235 | 66918 | 6 | 24320 | 41.1 |  | 00-00 00-00 00-00 00-00 00-00 3D-3D 00-00 00-00 | constant status |
| 0x32A | 33459 | 2 | 48434 | 20.6 |  | 00-00 2F-2F 00-00 00-00 00-00 00-00 00-00 00-00 | constant status |
| 0x351 | 26806 | 4 | 60658 | 16.5 |  | 00-00 00-00 00-00 00-00 00-00 00-00 00-00 00-00 | constant status |
| 0x361 | 25203 | 8 | 64527 | 15.5 | D1 D2 D3 D5 D7 D8 | 80-81 00-FF 00-7B 00-00 20-E0 00-00 00-4B 6F-8D | dynamic, likely brake/pedal/control status |
| 0x6C0 | 5428 | 8 | 299762 | 3.3 | D2 D4 | E4-E4 00-80 80-80 60-A0 00-00 00-00 00-00 00-00 | tentative body/lighting state |
| 0x58E | 1673 | 8 | 999324 | 1.0 | D6 | 00-00 00-00 00-00 00-00 00-00 00-06 00-00 00-00 | confirmed steering wheel buttons |
| 0x3B6 | 1669 | 8 | 1000682 | 1.0 | D3 D6 D7 D8 | 00-00 00-00 02-05 10-10 00-00 00-6E 03-AF 00-0D | cabin/AVN-adjacent candidate |
| 0x638 | 1646 | 8 | 1000040 | 1.0 | D2 D3 D5 | 13-13 00-80 00-0F 00-00 55-F5 00-00 00-00 00-00 | tentative body status |
| 0x3BD | 1633 | 1 | 999711 | 1.0 | D1 | 00-20 00-00 00-00 00-00 00-00 00-00 00-00 00-00 | cabin/AVN-adjacent candidate |
| 0x621 | 1628 | 8 | 1000062 | 1.0 | D2 D3 | 11-11 00-80 00-F2 00-00 00-00 00-00 00-00 00-00 | tentative lock/body event |

## Immediate Known Local Findings
- `0x58E` byte `D6` is confirmed steering wheel directional/enter/back button code from `steerbuttons1.csv` and existing firmware notes.
- `0x750`/`0x758` and `0x7B0`/`0x7B8` are diagnostic command/response channels for window and buzzer active tests, not normal passive cabin broadcasts.
- Existing DBCs name decimal `1568` / hex `0x620` as a seats/doors-style message, but the Prius v evidence points much more strongly at ambient light/illumination status on this capture.

## Decoded Or Characterized Signals
- `0x58E` steering wheel buttons: confirmed. `D6` values are `00 none`, `01 left`, `02 right`, `03 up`, `04 down`, `05 enter`, `06 back`. Full drive only exercised left/right/back, while `steerbuttons1.csv` confirms all six controls.
- `0x49B` drive mode status: confirmed from firmware and `mode buttons only.csv`. `D5` is the mode flag byte: bit1 EV, bit2 PWR, bit3 ECO. The mode-only capture shows `D5` values `00/02/04/06/08/0A`.
- `0x247` energy display: confirmed from firmware. `D1` low nibble is energy-flow state, `D2` is signed energy bar, and `D3` is a display/bar mode byte observed as `0x32` or `0xFF`.
- `0x620` ambient/illumination status: high confidence. `D3:D4` is raw ambient brightness (`D3 << 8 | D4`); focused brightness capture ranges roughly `0x0045..0x024F`. `D5` changes `B0/F0` in dim/brightness captures, so bit6 is the best dim-active candidate. Full drive mostly stayed at `B0`; `D8` had `00/10/40/50` states needing targeted light-switch correlation.
- `0x610` dimmer/rheostat cluster: medium confidence. Full drive shows `D3` ramping `0x00..0x4D` and `D4` usually `0x64`; local firmware treats `D4 == 0x00` as dimmer-down/display-off. The focused dim capture had only `20 00 00 64 C0 FF FD 74`, so it does not prove the display-off edge.
- `0x3B0` HVAC ACN1S07: medium confidence via Toyota reference DBC. In full drive, `D2` varies `0xA3..0xA7`, `D4` varies `0x49..0x4B`, and `D6` is mostly `0x11` or `0x01`. These are likely HVAC temp/status fields, but exact units need a deliberate HVAC-control capture.
- `0x381` HVAC ACN1S04: medium confidence via Toyota reference DBC. Mostly idle `00 00 00 10 00 00 00 00`; event frames include `D2=0x81`, `D3=0x01/0x02/0x0E`, and sometimes `D8=0x80`. This looks like HVAC button/status edges.
- `0x3A5` AVN1S13: medium confidence via Toyota reference DBC. `D7` walks through even values `0x00..0x3E`, matching a compass/heading-style raw field in the reference DBC.
- `0x621`: tentative lock/body event. Mostly idle `11 00 00 00 00 00 00 00`; only four event frames in full drive: `11 80 72 ...` and `11 80 F2 ...`. Existing README already suspected this ID is door lock/unlock related.
- `0x638` and `0x6C0`: tentative body/lighting candidates. Both are low-rate, mostly stable, and show short event flags. They are included in the overlay DBC as raw fields only.

## Overlay DBC
- Draft DBC: `SavvyCANstuff/prius_v_2016_body_misc_overlay.dbc`
- It intentionally includes duplicate IDs already present in other DBCs when this pass adds Prius v cabin/body semantics. Load it as an overlay or merge manually after validation.
- Confirmed signals have names and value tables; uncertain signals are raw bytes/bits with comments and confidence notes.

## Suggested Next Capture Script
This does not require perfect timing. The important part is to do one thing at a time, leave clear idle gaps, and repeat each action enough times that edges stand out statistically. Start SavvyCAN logging first, then do the sequence below in order. For every section, wait about 5 seconds at idle before the first action and 5 seconds after the last action.

### General Setup
1. Power the car to READY or IG-ON, whichever allows all cabin controls to work safely.
2. Leave HVAC on a known starting state: fan low, A/C off, auto off, front defog off, rear defog off, recirculation off, temperature at 72 F if possible.
3. Leave lights on auto/off at first, dash dimmer not at minimum, doors closed, seatbelt latched if convenient.
4. Start one continuous capture named something like `scripted cabin controls 1.csv`.

### Marker Pattern
Use the steering wheel buttons as human-readable timing markers because `0x58E` is already confirmed.

Before each major section:
1. Press Left once.
2. Wait 1 second.
3. Press Right once.
4. Wait 3 seconds.

After each major section:
1. Press Back once.
2. Wait 5 seconds.

### Section A: Steering Wheel Buttons
Purpose: reconfirm `0x58E` and create known markers.

1. Up, wait 1 second.
2. Down, wait 1 second.
3. Left, wait 1 second.
4. Right, wait 1 second.
5. Enter, wait 1 second.
6. Back, wait 1 second.
7. Repeat the same sequence once more.

### Section B: Drive Mode Buttons
Purpose: validate `0x49B D5` flags and any related text/display changes.

1. Press EV mode, wait 4 seconds.
2. Press EV mode again if it toggles back, wait 4 seconds.
3. Press ECO, wait 4 seconds.
4. Press PWR, wait 4 seconds.
5. Press Normal/default if there is a button or use ECO/PWR to return to normal, wait 4 seconds.
6. Repeat ECO -> PWR -> normal once more.

### Section C: Dash Dimmer And Illumination
Purpose: validate `0x610` dimmer/rheostat and `0x620` illumination/dim bits.

1. With exterior lights off or auto not dimmed, roll/click dash dimmer from current position to maximum, one step at a time, waiting 1 second per step.
2. Roll/click dash dimmer from maximum to minimum, one step at a time, waiting 1 second per step.
3. Hold or click to the absolute minimum/display-off position if available, wait 5 seconds.
4. Return to mid brightness, wait 5 seconds.
5. Turn parking/headlights on, wait 5 seconds.
6. Repeat dimmer max -> min -> mid with lights on.
7. Turn parking/headlights off, wait 5 seconds.

### Section D: Ambient Light Sensor
Purpose: validate `0x620 D3:D4` raw ambient brightness and dim-state transitions.

1. Leave dash dimmer at mid level.
2. Cover the dash/light sensor fully with a hand or opaque object, wait 10 seconds.
3. Uncover it, wait 10 seconds.
4. Shine a flashlight/phone light at it, wait 10 seconds.
5. Remove the light, wait 10 seconds.
6. Repeat cover -> uncover -> bright light -> normal once more.

### Section E: HVAC Buttons
Purpose: map `0x381`, `0x3B0`, and other ACN/HVAC messages.

Keep each press separated by about 3 seconds. If a button toggles, press once for on and once for off.

1. Fan up one step, repeat until max.
2. Fan down one step, repeat until low.
3. Temperature driver/passenger up 1 degree at a time for 5 steps.
4. Temperature driver/passenger down 1 degree at a time for 5 steps.
5. A/C on, wait 3 seconds; A/C off, wait 3 seconds.
6. Auto on, wait 3 seconds; Auto off/manual, wait 3 seconds.
7. Recirculation on, wait 3 seconds; fresh air, wait 3 seconds.
8. Mode/vent selection: face, face+feet, feet, feet+defog, defog, waiting 3 seconds each.
9. Front defog on, wait 5 seconds; off, wait 5 seconds.
10. Rear defog on, wait 5 seconds; off, wait 5 seconds.
11. Climate off, wait 5 seconds; climate on, wait 5 seconds.

### Section F: Display / Text / AVN Buttons
Purpose: look for text/menu/display traffic, especially AVN/MET messages.

If the head unit or meter has setup/display/trip buttons, use one button at a time.

1. Press Display or Setup, wait 5 seconds.
2. Navigate down one item at a time for 5 presses, waiting 2 seconds each.
3. Navigate up one item at a time for 5 presses, waiting 2 seconds each.
4. Enter/select one menu item, wait 5 seconds.
5. Back/return out, wait 5 seconds.
6. Cycle trip/display pages one press at a time for 10 presses, waiting 2 seconds each.
7. Change a units/display setting only if it is easy to restore, wait 5 seconds, then change it back.

### Section G: Door, Lock, Hatch, Seatbelt
Purpose: validate `0x621` and other body-event candidates.

Do this parked. Keep the car safe and avoid driving with doors open.

1. Driver door open, wait 5 seconds; close, wait 5 seconds.
2. Passenger front door open, wait 5 seconds; close, wait 5 seconds.
3. Driver rear door open, wait 5 seconds; close, wait 5 seconds.
4. Passenger rear door open, wait 5 seconds; close, wait 5 seconds.
5. Hatch open, wait 5 seconds; close, wait 5 seconds.
6. Lock doors from driver switch, wait 5 seconds; unlock, wait 5 seconds.
7. Lock/unlock from key fob if safe and the car state permits it.
8. Driver seatbelt unlatch, wait 5 seconds; latch, wait 5 seconds.
9. Passenger seatbelt latch/unlatch with someone or a buckle insert only if convenient.

### Section H: Lighting And Wipers
Purpose: isolate body/lighting candidates like `0x6C0`, `0x638`, and related messages.

1. Parking lights on, wait 5 seconds; off, wait 5 seconds.
2. Headlights on, wait 5 seconds; off, wait 5 seconds.
3. High beams on, wait 5 seconds; off, wait 5 seconds.
4. Fog lights on if equipped, wait 5 seconds; off, wait 5 seconds.
5. Left turn signal for 5 flashes, wait 5 seconds.
6. Right turn signal for 5 flashes, wait 5 seconds.
7. Hazards on for 10 seconds; off, wait 5 seconds.
8. Wiper mist once, wait 5 seconds.
9. Wiper intermittent low setting for 10 seconds; off, wait 5 seconds.
10. Wiper low for 10 seconds; off, wait 5 seconds.
11. Wiper high for 10 seconds; off, wait 5 seconds.
12. Washer spray briefly, wait 10 seconds.

### Section I: Windows And Buzzer Commands
Purpose: keep already reverse-engineered command channels separated from passive body traffic.

Only do this if you want a combined passive/command capture. Otherwise skip it.

1. Driver window down for 1 second, wait 5 seconds; up for 1 second, wait 5 seconds.
2. Passenger front window down/up the same way.
3. Rear windows down/up the same way.
4. Wireless buzzer on for 1 second, off, wait 5 seconds.
5. Combination meter buzzer on for 1 second, off, wait 5 seconds.

### Section J: End Marker
Purpose: make the end easy to locate.

1. Press Left, Right, Left, Right with 1 second between presses.
2. Wait 10 seconds.
3. Stop the capture.

### Notes For Analysis
- If timing is imperfect, do not restart. Repetition and idle gaps matter more than exact seconds.
- If something accidentally happens out of order, say so in the filename or in a small note next to the capture.
- One long capture is useful, but separate captures by section are even easier to analyze if stopping/starting SavvyCAN is convenient.

## Generated Artifacts
- `analysis/full_drive_1_id_summary.csv`: per-ID statistics for the main log.
- `analysis/capture_id_matrix.csv`: frame-count matrix across the main and focused captures.
