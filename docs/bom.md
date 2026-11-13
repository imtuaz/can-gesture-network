# Bill of materials

Prices are rough estimates for common hobby parts in Ho Chi Minh City (Nov 2026). Check before buying.

## Must buy

| Part | Qty | Est. price (VND) | Notes |
|------|-----|------------------|-------|
| ESP32 DevKit V1 (ESP32-WROOM-32, 30 or 38 pin) | 1 | 120–160k | Has built-in TWAI (CAN) controller. Prefer CP2102 USB chip |
| STM32F103C8T6 "Blue Pill" | 1 | 80–120k | Ask for a **genuine ST chip** (clones like CKS32/HK32 can fail with CubeIDE) |
| ST-Link V2 (mini USB stick) | 1 | 60–90k | To flash/debug the Blue Pill |
| SN65HVD230 CAN transceiver module (3.3 V) | 2 | 40–60k each | **3.3 V version**, not TJA1050 (5 V) |
| MPU6050 module (GY-521) | 1 | 40–60k | For milestone 6–7 |
| USB logic analyzer 24 MHz 8 ch | 1 | 120–180k | Works with PulseView (sigrok) |
| USB–UART adapter CP2102 (3.3 V) | 1 | 30–50k | To read STM32 printf output |
| Breadboard 830 holes | 2 | 25–35k each | One per node |
| Jumper wires male–male (20 cm) | 1 set (40) | 15–25k | Already have 20 male–female |
| Micro-USB data cables | 2 | 15–30k each | Must be **data** cables, not charge-only |

## Small parts

| Part | Qty | Est. price | Notes |
|------|-----|------------|-------|
| LED 5 mm (red/green) | 4 | ~2k | |
| Resistor 330 Ω | 4 | ~1k | LED current limit |
| Resistor 120 Ω | 2 | ~1k | CAN termination, only if the modules don't have it |
| Push button 6x6 | 2 | ~2k | |

**Estimated total: ~650–850k VND.**

Borrowing the Blue Pill + ST-Link from a friend/lab saves ~150–200k.

## Before buying, check

- [ ] SN65HVD230 module: does it have a 120 Ω resistor on board (often marked R2, "121")?
- [ ] Blue Pill: chip marked **STM32F103C8T6** with the ST logo
- [ ] ESP32: plain ESP32 (WROOM-32). ESP32-C3/S3 also have TWAI but pin-outs differ
