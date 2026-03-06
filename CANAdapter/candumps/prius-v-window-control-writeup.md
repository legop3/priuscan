# 2016 Prius v (Gen3) OBD-II Control and Input Catalog

## Scope
This is the working command catalog for output controls discovered from Techstream captures and replayed on-vehicle.

Current confirmed outputs:
- Power windows
- Combination meter buzzer
- Wireless buzzer (outside lock/unlock beeper)

Current confirmed inputs:
- Steering wheel directional/confirm/back buttons

This document is structured to keep adding new output tests as they are reversed.

## Vehicle and Bus Context
- Vehicle: 2016 Toyota Prius v (Gen3), Four trim, non-JBL
- Access point: DLC3 (OBD-II) pins 6/14 (HS-CAN)
- CAN: 11-bit standard IDs at 500 kbps

## Validation Method
Each catalog entry is considered confirmed only after:
1. Capture of Techstream active test traffic.
2. Isolation of candidate command frames.
3. Raw replay in SavvyCAN.
4. Verification that output works with and without Techstream session (when tested).

## Command Catalog

### 1) Power Windows
Command channel:
- Request ID: `0x750`
- Response ID: `0x758`

Door subaddresses:
- `0x90` = Driver front
- `0x91` = Passenger front
- `0x93` = Driver rear
- `0x92` = Passenger rear

Frame format:
- Down: `SS 04 30 01 01 40 00 00`
- Up: `SS 04 30 01 01 80 00 00`
- Stop: `SS 04 30 01 01 00 00 00`
- `SS` = one of `90/91/92/93`

Observed ACK:
- `0x758`: `SS 02 70 01 00 00 00 00`

Observed behavior:
- `0x40` = down
- `0x80` = up
- `0x00` = stop
- A single move frame causes about a 1-second motion pulse before ECU-side auto-stop.
- Explicit stop frame still works and should be sent.

Session/keepalive notes:
- Techstream used `SS 01 3E 00 00 00 00 00` and got `SS 01 7E 00 00 00 00 00`.
- Replay tests confirmed window control works without sending keepalive.

Timing guidance:
- Use pulse-based control.
- Repeat move frame every 700-900 ms for longer travel.
- Send explicit stop at end of command.

### 2) Combination Meter Buzzer
This is the gauge cluster beeper active test (repeating short beeps) with ON/OFF control.

Command channel:
- Request ID: `0x7B0`
- Response ID: `0x7B8`

Frame format:
- ON: `07 30 07 00 01 01 00 00`
- OFF: `07 30 07 00 01 00 00 00`

Observed ACK:
- `0x7B8`: `02 70 07 00 00 00 00 00`

Session/keepalive notes:
- Replay confirmed ON/OFF works from raw frames alone.
- No keepalive or extra session setup was required in testing.

### 3) Wireless Buzzer (Outside Beeper)
This is the external buzzer used for lock/unlock chirps. In Techstream active test, ON beeps continuously until OFF.

Command channel:
- Request ID: `0x750`
- Response ID: `0x758`

Subaddress and service:
- Subaddress: `0x40`
- Service: `0x30`
- Local identifier: `0x14`

Frame format:
- ON: `40 04 30 14 00 80 00 00`
- OFF: `40 04 30 14 00 00 00 00`

Observed ACK:
- `0x758`: `40 02 70 14 00 00 00 00`

Session/keepalive notes:
- Replay confirmed ON/OFF works from raw frames.
- No explicit keepalive/session-open frame was required during testing.
- Techstream was observed periodically re-sending OFF while the test was left in OFF state.

## Input Catalog

### 1) Steering Wheel Buttons (Directional + Enter + Back)
This is a read-only CAN broadcast signal (not an active test command).

Observed signal:
- CAN ID: `0x58E`
- Data length: 8
- Button code byte: `D6` (6th data byte in the CSV naming)

Decode rule:
- `D6 == 0x00` means no button press (idle)
- Non-zero `D6` values map to button events

Confirmed mapping from `steerbuttons1.csv` test order:
- `0x03` = Up
- `0x04` = Down
- `0x01` = Left
- `0x02` = Right
- `0x05` = Enter
- `0x06` = Back

Observed behavior:
- Each press generates repeated frames with the same non-zero code while held/pressed.
- Signal returns to `0x00` between presses.
- This is suitable for edge-triggered logic in firmware (`0x00 -> non-zero` transition).

## Program Integration Guidance
Use a command-catalog approach in firmware:
1. Keep command definitions as constants grouped by function (`windows`, `buzzer`, future outputs).
2. Implement non-blocking schedulers for pulsed outputs (windows) and direct toggles (buzzer).
3. Centralize a safety gate so all output commands can be inhibited by vehicle state or user override.
4. Log ACK IDs (`0x758`, `0x7B8`) for command acceptance telemetry.

Recommended minimal API surface:
- `window_set(window_id, direction)` where direction is `up/down/stop`
- `window_group_set(group_id, direction, duration_ms)` for pair/all behavior
- `buzzer_set(on_off)`

## Safety Notes
- Always provide a hard stop path for moving outputs.
- Keep command rates low; do not burst commands.
- Treat missing ACK as fault and fail safe to stop for motion outputs.
- Test single outputs first before grouped commands.

## Known Capture Files
- Windows: `candumps/windowdump1.csv`
- Buzzer: `candumps/buzzertest1.csv`
- Wireless buzzer: `candumps/wirelessbuzzer1.csv`
- Steering buttons: `candumps/steerbuttons1.csv`
- Supplemental notes: `candumps/OBD-II Window Control on 2016 Prius v (Gen3) – Architecture & Diagnostics.pdf`

## Open Items
- Reverse additional Techstream outputs into this same catalog format.
- Confirm moonroof channel/command mapping.
- Determine any edge preconditions for each output (IGN state, retained power, lock state).
