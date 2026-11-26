# Milestone 8 – Fault detection and handling: test plan

Firmware: `firmware/node1_esp32/m8_fault_tx` (ESP32) and `firmware/node2_stm32/m8_fault_rx` (STM32).
Shared E2E code: `firmware/common/e2e.h`, host test `tests/test_e2e.c`.

> Note: this is **fault detection and handling on high-speed CAN (ISO 11898-2)**, not the
> ISO 11898-3 "fault-tolerant" physical layer. The SN65HVD230 cannot keep running with a broken
> or shorted wire; the goal is that both nodes notice the fault, go to a safe state and recover.

## Setup

- Open two terminals: ESP32 Serial Monitor (COM6) and the STM32 UART via CP2102 (COM17), both 115200.
- STM32 commands can be typed on its UART (needs PA10 ← CP2102 TX) or sent from the ESP32 over CAN
  ID `0x7E0`: ESP32 `h` → STM32 hang, `x` → STM32 stops `0x210`, `y` → resumes.
- Logic analyzer: D0 → PB8 (STM32 RX), D1 → PB9 (STM32 TX), 8 MHz, as in milestone 4.
- Before each test: both sides NORMAL, TEC = REC = 0.

## Expected behaviour (write the measured value next to each prediction)

| # | Fault injected | How | Expected on STM32 | Expected on ESP32 | Measured |
|---|---|---|---|---|---|
| T1 | ESP32 software stops sending | ESP32 `s`, later `n` | SAFE ~300 ms after the last 0x110, LED off; back to NORMAL after 3 valid frames (~300 ms). TEC stays 0 (ESP32 hardware still ACKs) | — | ✅ SAFE + LED off, NORMAL ~300 ms after `n`, ESP32 TEC 0 |
| T2 | Corrupted payload | ESP32 `c` | 1 frame dropped (`crc=1`), DEGRADED for 1 s, LED not affected | — | ✅ DEGRADED 17187 → NORMAL 18187 ms (1000 ms) |
| T3 | Sender stuck on old data | ESP32 `f`, later `n` | `lap` counts up, no valid data → SAFE after 300 ms. Without E2E the STM32 would keep acting on stale data | — | ✅ DEGRADED (0x02) on the first repeat, SAFE (0x03) 200 ms later, LED off; after `n`: DEGRADED until 1 s after the last E2E error, then NORMAL |
| T4 | One frame lost | ESP32 `k` | `E2E: mat 1 frame`, frame still used, stays NORMAL | — | ✅ STM32: `E2E: mat 1 frame (van dung frame nay)`, lost counter 6 → 7, stays NORMAL ([screenshot](../captures/m8_stm32_uart_t4.png)) |
| T5 | STM32 unplugged (pull CANH+CANL of node 2) | wires | — | ACK errors: TEC rises to 128 → ERROR PASSIVE and **stops at 128, no bus-off**; "MAT STM32" after 300 ms | ✅ (STM32 held in reset / not running): TEC stays 128, TX queue 6, ~4 450 bus errors/s, busoff = 0; after the ACK returns TEC −1 per frame (−10/s) |
| T6 | ESP32 unplugged | wires | timeout → SAFE; own 0x210 gets no ACK → TEC 128, ERROR PASSIVE, the 3 mailboxes fill (`tx ban` counts up) | — | ESP32 side ✅ (TEC 128, queue 6, MAT, no bus-off). Found stale frames after re-plugging, see below |
| T7 | CANH shorted to CANL (jumper, a few seconds) | jumper | bit errors (LEC "bit dominant"), BUS-OFF, ABOM recovers every few ms while the short is present → SAFE | BUS-OFF; 3 fast recoveries, then one attempt per second | ✅ ESP32: 3 recoveries ~100 ms apart, then 1 per second; STM32 reports bus-off (flag 0x08) |
| T8 | Remove the short | jumper off | recovery, ERROR ACTIVE, NORMAL after 3 frames | "Phuc hoi xong sau … us" ≥ 2816 µs | ✅ 2895–3054 µs on a silent bus; ~9.3 ms when the STM32 was transmitting (its frames break the 11-recessive-bit sequences) |
| T9 | STM32 software hang | ESP32 `h` (or STM32 UART `h`) | IWDG reset after ~0.33–0.67 s, banner "do WATCHDOG reset" | "MAT STM32", then STM32 back with flag 0x10 | ✅ MAT 300 ms after the last 0x210; reset ≈ 0.57 s after the hang (→ LSI ≈ 35 kHz); back on the bus ~13 ms later with flag 0x10; NORMAL 280 ms later |
| T10 | One wire open (CANL unplugged, CANH still connected) | wire | — | Not covered by ISO 11898-2: behaviour depends on the floating CANL | ⚠️ Unstable: link kept working for ~2 s (NORMAL, TEC falling), then no ACK (TEC stuck at 147, MAT); bit errors while the contact changed (TEC up to 192, REC 8). Earlier, plugging the wires back one at a time caused 5 bus-offs |

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
- **T10, one wire open.** A receiver reads CANH − CANL. With CANL open, the far node sees only part of
  the differential voltage, close to the 0.5–0.9 V threshold of the SN65HVD230, so some bits are read right
  and some wrong. A half-broken bus causes bit errors (TEC past 128, even bus-off), which is worse than a
  cleanly unplugged node (ACK errors only, TEC stops at 128). An ISO 11898-3 fault-tolerant transceiver
  would switch to single-wire mode here; a high-speed transceiver cannot.
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

## Findings from run 1 (log: `captures/m8_esp32_log_run1.txt`)

- **Stale frames after bus-off.** Right after recovery the ESP32 received `0x210` reporting NORMAL,
  immediately followed by SAFE/bus-off. The first frame had been waiting in a bxCAN mailbox since before
  the fault and was retransmitted automatically after recovery. Fix: on bus-off the STM32 aborts all
  pending mailboxes (`ABRQ0..2` in `CAN_TSR`).
- **REC rising during the short.** With a hard short the bus would read only recessive, yet the ESP32 REC
  climbed to 128 between bus-offs. The jumper contact was imperfect, so some dominant bits got through
  and the receiver saw malformed frames.
- **Bring-up mistake (not a CAN fault):** `IWDG_PR/RLR` written before starting the IWDG; LSI is off
  until `0xCCCC`, so `IWDG_SR` never cleared and the CPU hung. The bxCAN still ACKed in hardware,
  so the only symptom was ESP32 "TEC = 0 but no 0x210".
- **Stale frames after a lost ACK (T6).** After re-plugging, the ESP32 first received three old `0x210`
  frames (NORMAL / DEGRADED / NORMAL) that had been stuck in the STM32 mailboxes, and the STM32 accepted
  the old `0x110` frames still waiting in the ESP32 TX queue: their counters were still consecutive,
  so E2E could not tell they were old. An alive counter detects repeated and lost frames, not delayed ones.
  Fix: when nothing can be queued (no ACK), the STM32 aborts its mailboxes and the ESP32 clears its TX
  queue, so the first frame after the fault is fresh data.
- **ABOM vs. a recovery strategy, in numbers.** After the CANH–CANL short tests the STM32 counted
  **1435 bus-offs** (hardware ABOM recovers in ~3 ms and fails again while the fault is present), while the
  ESP32, with 3 fast recoveries and then one per second, counted 19 over the same tests.
