# Notes and lessons learned

Write down problems and how they were solved. This is what interviewers like to hear about.

| Date | Problem | Cause | Fix |
|------|---------|-------|-----|
| 2026-11-21 | Garbage characters on UART | CP2102 header pins not soldered, intermittent contact | Soldered the pins |
| 2026-11-21 | TWAI loopback: RX pin unplugged → REC 129, TX queue stuck at 6 | Floating RX reads dominant, node becomes receiver and never sees bus idle | Re-plug jumper; REC only drops when frames from *other* nodes are received |
| 2026-11-21 | `twai_transmit()` returned OK but nothing on the bus | It only queues the frame (5 queue + 1 HW buffer) | Check `twai_get_status_info()` (tx queue, TEC, bus errors) |
| 2026-11-23 | ESP32 TEC stuck at 128, no ACK | Blue Pill had been built but not flashed — still ran milestone 1 code | Flash before testing. ACK errors don't raise TEC once error-passive, hence 128 |
| 2026-11-23 | Still no ACK after flashing | CANH wired to MCU RX pins, CANL unconnected | CTX/CRX go to the MCU; CANH↔CANH and CANL↔CANL between the two transceivers only |
| 2026-11-23 | "CAN init OK" while module A RX wire was open | PB8 pull-up makes the bus look idle | Init OK does not prove the transceiver path works |
| 2026-11-23 | Switched STM32 to HSE | CAN tolerates ~0.5 % clock error, HSI is ±1 % | Enable HSE in `SystemInit`, still 8 MHz so BRR/BTR unchanged |
| 2026-11-23 | PulseView "Capture failed", ESP32 port dropped | Windows "USB device not recognized" (USB glitch) | Re-plug devices; first capture triggered on a glitch, so capture longer instead of trusting the trigger |
