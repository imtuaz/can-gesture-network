# Hardware

| Part | Qty | Notes |
|------|-----|-------|
| ESP32 dev board (ESP32-D0WD-V3, 38 pin) | 1 | Node 1, built-in TWAI (CAN) controller |
| STM32F103C8T6 "Blue Pill" | 1 | Node 2, bxCAN remapped to PB8/PB9. Device ID checked over SWD: 0x410 |
| ST-Link V2 | 1 | Flashing and debugging the Blue Pill |
| SN65HVD230 CAN transceiver module (3.3 V) | 2 | One per node, 120 Ω termination on each module |
| MPU6050 module (GY-521) | 1 | Tilt (milestone 6) and gesture data (milestone 7) |
| USB logic analyzer, 8 channels, 24 MHz | 1 | PulseView (sigrok), probes on CAN_TX / CAN_RX |
| CP2102 USB–UART adapter (3.3 V) | 1 | STM32 log output |
| Push button | 1 | Milestone 5 |
| Breadboards, jumper wires, micro-USB data cables | | |

## Checks done before wiring

- Each SN65HVD230 module has a 120 Ω resistor between CANH and CANL; with two nodes the bus measures ~60 Ω.
- Blue Pill chip identified as a genuine STM32F103 (Device ID 0x410, medium density, 64 KB flash).
- The logic analyzer is connected on the 3.3 V logic side (CTX/CRX), never on CANH/CANL.
