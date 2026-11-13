# CAN Gesture-Control Network (ESP32 + STM32)

> **Status: in progress** (started Nov 2026)

A two-node CAN bus built with real transceivers:

- **Node 1 – ESP32** (built-in TWAI/CAN controller): reads an MPU6050 and sends commands on the bus.
- **Node 2 – STM32F103 "Blue Pill"** (bxCAN): receives frames and drives LEDs / reports over UART.
- Bus traffic is captured with a USB logic analyzer and decoded with my
  [protocol-analyzer](https://github.com/imtuaz/protocol-analyzer) (bit destuffing, CRC-15 check).

Later stage: on-device gesture recognition (TinyML) on the ESP32, so a hand gesture
becomes a CAN command.

```
 [Node 1: ESP32 + MPU6050]                 [Node 2: STM32 Blue Pill]
        |  TWAI TX/RX                              |  CAN TX/RX (PB9/PB8)
   [SN65HVD230]                               [SN65HVD230]
        |                                          |
 =======+===== CAN_H / CAN_L (120 ohm at each end) =+=======
                         |
                 [USB logic analyzer] -> PulseView CSV -> protocol-analyzer
```

## Milestones

| # | Goal | Done when | Status |
|---|------|-----------|--------|
| 1 | STM32 blinks, prints over UART | "hello" seen in serial terminal | ☐ |
| 2 | ESP32 TWAI in loopback mode | frame sent and received by itself | ☐ |
| 3 | ESP32 -> STM32 over real bus | STM32 prints received ID + data | ☐ |
| 4 | Capture and decode | logic analyzer CSV decoded by protocol-analyzer, CRC OK | ☐ |
| 5 | Button -> LED over CAN | pressing a button on node 1 toggles an LED on node 2 | ☐ |
| 6 | MPU6050 tilt -> CAN | tilt angle sent every 100 ms | ☐ |
| 7 | Gesture recognition (TinyML) | ≥ 3 gestures recognized on ESP32 and sent as CAN commands | ☐ |

## CAN message plan

| ID | Sender | DLC | Data | Meaning |
|----|--------|-----|------|---------|
| `0x100` | Node 1 | 1 | button state | milestone 5 |
| `0x110` | Node 1 | 4 | roll, pitch (int16, 0.01°) | milestone 6 |
| `0x120` | Node 1 | 1 | gesture id | milestone 7 |
| `0x200` | Node 2 | 1 | heartbeat counter | node 2 alive |

Bit rate: **500 kbit/s**.

## Repository layout

```
docs/       wiring, bill of materials, notes and lessons learned
firmware/
  node1_esp32/   ESP-IDF (or Arduino) project for the ESP32
  node2_stm32/   STM32 project (CubeIDE or SPL)
captures/   logic analyzer captures (CSV) and decoded reports
```

See [docs/wiring.md](docs/wiring.md) and [docs/bom.md](docs/bom.md).

## Author

Hồ Anh Tuấn – University of Science, VNU-HCM
