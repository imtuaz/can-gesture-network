# Wiring

All parts run at **3.3 V**. Connect all GNDs together (both boards, transceivers, logic analyzer).

## Node 1 – ESP32

| ESP32 pin | Connect to | Notes |
|-----------|-----------|-------|
| 3V3 | SN65HVD230 VCC, MPU6050 VCC | |
| GND | SN65HVD230 GND, MPU6050 GND | |
| GPIO5 | SN65HVD230 **CTX** (TX) | TWAI TX (any free GPIO works, set in code) |
| GPIO4 | SN65HVD230 **CRX** (RX) | TWAI RX |
| GPIO21 | MPU6050 SDA | default I2C SDA |
| GPIO22 | MPU6050 SCL | default I2C SCL |
| GPIO18 | push button to GND | internal pull-up |

## Node 2 – STM32F103 Blue Pill

| Blue Pill pin | Connect to | Notes |
|---------------|-----------|-------|
| 3.3 | SN65HVD230 VCC | |
| GND | SN65HVD230 GND | |
| **PB9** | SN65HVD230 **CTX** | CAN_TX after **remap** |
| **PB8** | SN65HVD230 **CRX** | CAN_RX after **remap** |
| PA9 (USART1 TX) | CP2102 RX | printf to PC |
| PA10 (USART1 RX) | CP2102 TX | |
| PC13 | on-board LED | |
| PB0 | LED + 330 Ω to GND | |

> ⚠️ The default CAN pins PA11/PA12 are shared with **USB** on the Blue Pill.
> Use the remap to PB8/PB9 (`AFIO->MAPR` CAN remap 2, or "CAN_RX: PB8" in CubeMX).

## CAN bus

| | Node 1 transceiver | Node 2 transceiver |
|--|--|--|
| CANH | ⟷ | CANH |
| CANL | ⟷ | CANL |

- Use a short twisted pair (twist two jumper wires).
- Exactly **two 120 Ω** terminations, one at each end of the bus. If both modules already
  have one on board, add nothing.
- Measure with a multimeter (power OFF) between CANH and CANL: **~60 Ω** means correct
  (two 120 Ω in parallel).

## Logic analyzer

| Analyzer | Connect to | Why |
|----------|-----------|-----|
| CH0 | ESP32 GPIO5 (TX, logic side) | clean 0–3.3 V digital signal |
| CH1 | STM32 PB8 (RX, logic side) | what node 2 sees |
| GND | common GND | |

Probe the **logic side** (CTX/CRX), not CANH/CANL: the analyzer reads single-ended 0–3.3 V,
while CANH/CANL is a differential signal.

Sample rate: at least **10× the bit rate** → 500 kbit/s needs ≥ 5 MHz; use 8 or 12 MHz in PulseView.
Export as CSV, then:

```bash
./build/pa analyze capture.csv can --bitrate 500000 --report report.html
```
