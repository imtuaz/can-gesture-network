# Milestone 8 – Fault detection and handling: test plan

Firmware: `firmware/node1_esp32/m8_fault_tx` (ESP32) and `firmware/node2_stm32/m8_fault_rx` (STM32).
Shared E2E code: `firmware/common/e2e.h`, host test `tests/test_e2e.c`.

> Note: this is **fault detection and handling on high-speed CAN (ISO 11898-2)**, not the
> ISO 11898-3 "fault-tolerant" physical layer. The SN65HVD230 cannot keep running with a broken
> or shorted wire; the goal is that both nodes notice the fault, go to a safe state and recover.

## Setup

- Open two terminals: ESP32 Serial Monitor (COM6) and the STM32 UART via CP2102 (COM17), both 115200.
- **PA10 must be connected to CP2102 TX** to type STM32 commands (`s`, `n`, `h`, `r`).
- Logic analyzer: D0 → PB8 (STM32 RX), D1 → PB9 (STM32 TX), 8 MHz, as in milestone 4.
- Before each test: both sides NORMAL, TEC = REC = 0.

## Expected behaviour (write the measured value next to each prediction)

| # | Fault injected | How | Expected on STM32 | Expected on ESP32 | Measured |
|---|---|---|---|---|---|
| T1 | ESP32 software stops sending | ESP32 `s`, later `n` | SAFE ~300 ms after the last 0x110, LED off; back to NORMAL after 3 valid frames (~300 ms). TEC stays 0 (ESP32 hardware still ACKs) | — | |
| T2 | Corrupted payload | ESP32 `c` | 1 frame dropped (`crc=1`), DEGRADED for 1 s, LED not affected | — | |
| T3 | Sender stuck on old data | ESP32 `f`, later `n` | `lap` counts up, no valid data → SAFE after 300 ms. Without E2E the STM32 would keep acting on stale data | — | |
| T4 | One frame lost | ESP32 `k` | `E2E: mat 1 frame`, frame still used, stays NORMAL | — | |
| T5 | STM32 unplugged (pull CANH+CANL of node 2) | wires | — | ACK errors: TEC rises to 128 → ERROR PASSIVE and **stops at 128, no bus-off**; "MAT STM32" after 300 ms | |
| T6 | ESP32 unplugged | wires | timeout → SAFE; own 0x210 gets no ACK → TEC 128, ERROR PASSIVE, the 3 mailboxes fill (`tx ban` counts up) | — | |
| T7 | CANH shorted to CANL (jumper, a few seconds) | jumper | bit errors (LEC "bit dominant"), BUS-OFF, ABOM recovers every few ms while the short is present → SAFE | BUS-OFF; 3 fast recoveries, then one attempt per second | |
| T8 | Remove the short | jumper off | recovery, ERROR ACTIVE, NORMAL after 3 frames | "Phuc hoi xong sau … us" ≥ 2816 µs | |
| T9 | STM32 software hang | STM32 `h` | IWDG reset after ~0.33–0.67 s, banner "do WATCHDOG reset" | "MAT STM32", then STM32 back with flag 0x10 | |

## Why some results differ (to explain in an interview)

- **T5 vs T7.** Missing ACK and a shorted bus both look like "the bus is broken", but CAN treats them
  differently. With no ACK, a transmitter that is already error passive does not increase TEC any more
  (ISO 11898-1), so a lonely node stays at 128 and never goes bus-off: it may simply be the only node
  powered up. With CANH–CANL shorted, the differential voltage is 0 V, so the bus always reads
  recessive. The node sends a dominant bit, reads recessive back: **bit error**, TEC +8 each time,
  bus-off after at most 32 attempts.
- **T7, two recovery strategies.** The STM32 uses hardware ABOM: it comes back as soon as it sees
  128 × 11 recessive bits. A shorted bus *is* recessive, so it recovers in ~3 ms and fails again,
  over and over. The ESP32 recovers 3 times quickly, then waits 1 s, so a permanent fault does not
  keep the node hammering the bus.
- **T1 vs T6.** Both trip the STM32 timeout, but only T6 also raises the STM32 TEC, because in T1 the
  ESP32 CAN controller is still on the bus and ACKs every frame. The timeout alone cannot tell them
  apart; the error counters can.
- **T3.** CAN's CRC-15 is valid on every frozen frame, because the frame itself is correct on the wire.
  Only the alive counter shows that the data is not new.

## Measurements to capture

- Detection latency of T1 (last 0x110 on the analyzer → STM32 log line / LED off).
- Number of attempts until bus-off in T7 (count error frames on D1).
- Bus-off recovery time in T8 (ESP32 log, and on the analyzer: last error frame → first good frame).
- IWDG reset time in T9.
