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
| 2026-11-23 | PulseView "Capture failed", later "Scan" finds nothing and falls back to Demo device | After a Windows "USB device not recognized" event the analyzer was no longer bound to WinUSB, so libusb could not open it (scan shows an empty list, no error) | Zadig → List All Devices → select the analyzer → WinUSB → Replace Driver. First capture also triggered on a glitch, so capture longer instead of trusting the trigger |
| 2026-11-23 | Milestone 5: STM32 reply took 3–4 ms | Blocking UART log (44 chars × 86.8 µs at 115200 = 3.8 ms) printed *before* sending the CAN reply | Send the reply first, log after → reply < 1 ms |
| 2026-11-23 | ESP32 sketch failed: `GPIO_NUM_25` not declared | Arduino IDE board set to ESP32-S2 (no GPIO25) | Board → ESP32 Dev Module |
| 2026-11-23 | Milestone 6: LED toggles briefly while shaking the sensor near 0° | Tilt is computed from the accelerometer only; hand acceleration adds to gravity | Expected limitation; fuse with gyro (complementary filter) in milestone 7 |
| 2026-11-25 | First gesture dataset: model ~78 % | Gestures too slow, peak rotation ~25 °/s vs ~10 °/s at rest | Logger rejects samples below 100 °/s and prints the peak; recollected → 97 % |
| 2026-11-25 | `gesture_model.h` did not compile: `189f` | Python printed whole numbers without a decimal point | Always emit `189.0f`; caught by compiling a host-side C test against the Python reference |
| 2026-11-25 | `B1` redeclared | Arduino core defines binary constants `B0`, `B1`, ... | Prefix model arrays with `NN_` |
| 2026-11-25 | Gestures recognized but `Hang doi TX day`, no reply | Blue Pill not running (CubeIDE opened the ST-Link upgrade tool instead of flashing) | Skip the upgrade (clone ST-Link), flash; check the UART banner |
| 2026-11-25 | `khong doc duoc MPU6050` after wearing the sensor on the hand | No I2C ACK at 0x68; wiring/solder joints stressed by fast wrist motion (open) | Added I2C/zero-reading check with re-init; hardware fix pending |
