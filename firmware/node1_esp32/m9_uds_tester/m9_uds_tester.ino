/*
 * Moc 9 - phia ESP32: moc 8 + dong vai TESTER chan doan UDS (doc / xoa DTC cua STM32)
 *
 * MOI O MOC 9:
 *   Request UDS gui ID 0x7E0 (Single Frame ISO-TP), STM32 tra loi ID 0x7E8.
 *   Phan hoi dai hon 7 byte: STM32 gui First Frame -> ESP32 tra Flow Control (30 00 00:
 *   gui het, khong can cho) -> STM32 gui cac Consecutive Frame -> ESP32 ghep lai roi giai ma.
 *   Lenh Serial moi:
 *     d = doc DTC (19 02 FF)        e = dem DTC (19 01 FF)      z = xoa DTC (14 FF FF FF)
 *     p = TesterPresent (3E 00)     u = gui service khong ho tro (22 F1 90) -> phan hoi am
 *   Lenh gay loi cho STM32 (h / x / y) doi sang ID 0x7F0.
 *
 * ---- Phan moc 8 ----
 *
 * MPU6050: SDA = G21, SCL = G22. TWAI: G25 = TX -> CTX, G26 = RX <- CRX (module B), 500 kbit/s
 * e2e.h (chep tu firmware/common) dat cung thu muc voi file .ino nay.
 *
 * Gui:  0x110 DLC 6 moi 100 ms: roll (int16 LE, 0.01 do), pitch, alive counter, CRC-8
 * Nhan: 0x210 moi 100 ms tu STM32 [trang thai, TEC, REC, co loi] -> qua 300 ms khong thay
 *       thi bao "mat STM32". 0x200: trang thai LED.
 *
 * Xu ly bus-off (chien luoc phuc hoi 2 muc, giong cach ECU that lam):
 *   - 3 lan bus-off dau: phuc hoi ngay (twai_initiate_recovery)
 *   - tu lan thu 4 (trong vong 5 s): cho 1 s roi moi phuc hoi, tranh lam nghen bus
 *     khi loi van con (vi du day van dang bi chap)
 *   Phuc hoi = controller phai thay 128 lan x 11 bit lan = 1408 bit = 2.816 ms o 500 kbit/s.
 *
 * Lenh qua Serial Monitor (115200, go 1 ky tu roi Enter):
 *   n = binh thuong (tat moi loi gia lap)
 *   s = im lang: ngung gui 0x110 (phan mem bi "treo" nhung bo CAN van ACK)
 *   f = dong bang: gui lai y nguyen frame cu (counter khong tang)
 *   c = lam sai CRC cua 1 frame
 *   k = bo qua 1 counter (gia lap mat 1 frame)
 *   i = in trang thai ngay
 * Lenh cho STM32 (gui qua CAN 0x7F0, khong can noi PA10):
 *   h = STM32 treo CPU (thu watchdog)   x = STM32 ngung gui 0x210   y = STM32 gui lai 0x210
 */

#include <Wire.h>
#include <math.h>
#include "driver/twai.h"
#include "e2e.h"

#define PIN_TX      GPIO_NUM_25
#define PIN_RX      GPIO_NUM_26
#define PIN_SDA     21
#define PIN_SCL     22

#define MPU_ADDR        0x68
#define MPU_PWR_MGMT_1  0x6B
#define MPU_ACCEL_XOUT  0x3B

#define ID_TILT         0x110
#define ID_LED_STATUS   0x200
#define ID_NODE2_STATUS 0x210
#define ID_TEST_CMD     0x7F0   /* lenh gay loi gui cho STM32 */
#define ID_UDS_REQ      0x7E0   /* tester -> ECU */
#define ID_UDS_RESP     0x7E8   /* ECU -> tester */

#define PERIOD_MS           100
#define NODE2_TIMEOUT_MS    300
#define FAST_RECOVERIES     3
#define SLOW_RECOVERY_MS    1000
#define BUSOFF_FORGET_MS    5000

/* Che do gay loi */
enum { INJ_NONE, INJ_SILENT, INJ_FREEZE };
int inject = INJ_NONE;
bool inject_crc_once = false;
bool inject_skip_once = false;

uint8_t tx_counter = 0;
uint8_t last_frame[8];
bool have_last_frame = false;
unsigned long last_send = 0;

/* Bus-off */
bool bus_off = false;
bool recovering = false;
int busoff_recent = 0;
unsigned long busoff_ms = 0;
unsigned long recover_at_ms = 0;
unsigned long recover_start_us = 0;
uint32_t n_busoff = 0;

/* Thong ke */
uint32_t n_sent = 0, n_tx_fail = 0, n_bus_err = 0, n_queue_full = 0;

/* STM32 */
unsigned long node2_last_ms = 0;
bool node2_alive = false;
uint8_t node2_state = 255;
uint8_t node2_flags = 0;
const char *ST_NAMES[3] = {"NORMAL", "DEGRADED", "SAFE"};

unsigned long next_print = 0;

void handle_uds_frame(const twai_message_t &rx);   /* khai bao truoc, dinh nghia o phan UDS */

/* ================= MPU6050 ================= */
bool mpu_write(uint8_t reg, uint8_t value)
{
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool mpu_read_accel(int16_t *ax, int16_t *ay, int16_t *az)
{
  uint8_t b[6];
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_ACCEL_XOUT);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6) != 6) {
    return false;
  }
  for (int i = 0; i < 6; i++) {
    b[i] = Wire.read();
  }
  *ax = (int16_t)((b[0] << 8) | b[1]);
  *ay = (int16_t)((b[2] << 8) | b[3]);
  *az = (int16_t)((b[4] << 8) | b[5]);
  /* Ca 3 truc = 0 -> MPU6050 vua mat dien, dang ngu -> coi nhu loi va danh thuc lai */
  if (*ax == 0 && *ay == 0 && *az == 0) {
    mpu_write(MPU_PWR_MGMT_1, 0x00);
    return false;
  }
  return true;
}

/* ================= Gui 0x110 ================= */
void send_tilt()
{
  twai_message_t msg = {};
  msg.identifier = ID_TILT;
  msg.data_length_code = 6;

  if (inject == INJ_FREEZE && have_last_frame) {
    memcpy(msg.data, last_frame, 6);               /* gui lai y nguyen: counter dung yen */
  } else {
    int16_t ax, ay, az;
    if (!mpu_read_accel(&ax, &ay, &az)) {
      /* Cam bien hong -> khong gui. Ben nhan se timeout va vao SAFE:
       * khong co du lieu tot thi im lang, con hon gui du lieu sai */
      static unsigned long last_warn = 0;
      if (millis() - last_warn > 1000) {
        Serial.println("LOI: khong doc duoc MPU6050 -> khong gui 0x110");
        last_warn = millis();
      }
      return;
    }
    float roll  = atan2f((float)ay, (float)az) * 180.0f / PI;
    float pitch = atan2f(-(float)ax, sqrtf((float)ay * ay + (float)az * az)) * 180.0f / PI;
    int16_t r = (int16_t)lroundf(roll * 100.0f);
    int16_t p = (int16_t)lroundf(pitch * 100.0f);
    msg.data[0] = (uint8_t)(r & 0xFF);
    msg.data[1] = (uint8_t)((r >> 8) & 0xFF);
    msg.data[2] = (uint8_t)(p & 0xFF);
    msg.data[3] = (uint8_t)((p >> 8) & 0xFF);

    if (inject_skip_once) {                        /* "mat" 1 frame: tang counter ma khong gui */
      tx_counter = (tx_counter + 1) & 0x0F;
      inject_skip_once = false;
      Serial.println(">> Bo qua 1 counter");
    }
    e2e_protect(ID_TILT, msg.data, 4, &tx_counter);
    memcpy(last_frame, msg.data, 6);
    have_last_frame = true;
  }

  if (inject_crc_once) {
    msg.data[5] ^= 0x01;                           /* lat 1 bit cua CRC-8 */
    inject_crc_once = false;
    Serial.println(">> Gui 1 frame sai CRC-8");
  }

  esp_err_t e = twai_transmit(&msg, 0);
  if (e == ESP_OK) {
    n_sent++;
  } else {
    n_tx_fail++;                                   /* hang doi day, hoac dang bus-off */
    /* Hang doi day = khong ai ACK. 5 frame dang cho la goc nghieng CU: khi cam lai day chung
     * se duoc gui ra truoc va STM32 chap nhan (counter van lien tiep) -> dieu khien theo du lieu
     * cu. Xoa di de frame dau tien sau khi het loi la du lieu moi. (Counter da tang, nen STM32
     * se thay "nhay counter" -> dung la co frame bi mat.) */
    twai_clear_transmit_queue();
  }
}

/* ================= Alerts cua TWAI ================= */
void print_counters(const char *what)
{
  twai_status_info_t s;
  twai_get_status_info(&s);
  Serial.printf("[t=%lu ms] %s | TEC=%lu REC=%lu\n", millis(), what,
                s.tx_error_counter, s.rx_error_counter);
}

void handle_alerts()
{
  uint32_t a;
  if (twai_read_alerts(&a, 0) != ESP_OK) {
    return;                                        /* khong co alert moi */
  }

  if (a & TWAI_ALERT_BUS_ERROR)       n_bus_err++;
  if (a & TWAI_ALERT_TX_FAILED)       n_tx_fail++;
  if (a & TWAI_ALERT_RX_QUEUE_FULL)   n_queue_full++;

  if (a & TWAI_ALERT_ABOVE_ERR_WARN)  print_counters("ERROR WARNING (>= 96)");
  if (a & TWAI_ALERT_ERR_PASS)        print_counters("ERROR PASSIVE (>= 128)");
  if (a & TWAI_ALERT_ERR_ACTIVE)      print_counters("ERROR ACTIVE (binh thuong)");

  if (a & TWAI_ALERT_BUS_OFF) {
    bus_off = true;
    n_busoff++;
    if (millis() - busoff_ms > BUSOFF_FORGET_MS) {
      busoff_recent = 0;                           /* lau roi moi bi lai: tinh lai tu dau */
    }
    busoff_recent++;
    busoff_ms = millis();
    recover_at_ms = millis() + (busoff_recent <= FAST_RECOVERIES ? 0 : SLOW_RECOVERY_MS);
    Serial.printf("[t=%lu ms] BUS-OFF lan %d -> phuc hoi %s\n", millis(), busoff_recent,
                  busoff_recent <= FAST_RECOVERIES ? "ngay" : "sau 1 s");
  }

  if (a & TWAI_ALERT_BUS_RECOVERED) {
    unsigned long dt = micros() - recover_start_us;
    bus_off = false;
    recovering = false;
    twai_start();                                  /* sau phuc hoi controller o STOPPED */
    Serial.printf("[t=%lu ms] Phuc hoi xong sau %lu us (ly thuyet >= 2816 us)\n", millis(), dt);
  }
}

void handle_busoff_recovery()
{
  if (bus_off && !recovering && (long)(millis() - recover_at_ms) >= 0) {
    if (twai_initiate_recovery() == ESP_OK) {
      recovering = true;
      recover_start_us = micros();
    }
  }
}

/* ================= Nhan ================= */
void handle_rx()
{
  twai_message_t rx;
  while (twai_receive(&rx, 0) == ESP_OK) {
    if (rx.identifier == ID_NODE2_STATUS && rx.data_length_code >= 4) {
      node2_last_ms = millis();
      if (!node2_alive) {
        node2_alive = true;
        Serial.printf("[t=%lu ms] STM32 lien lac lai\n", millis());
      }
      if (rx.data[0] != node2_state && rx.data[0] < 3) {
        Serial.printf("[t=%lu ms] STM32 trang thai: %s (co loi 0x%02X)\n", millis(),
                      ST_NAMES[rx.data[0]], rx.data[3]);
      }
      if ((rx.data[3] & 0x10) && !(node2_flags & 0x10)) {   /* chi in 1 lan khi co moi bat */
        Serial.printf("[t=%lu ms] STM32 khoi dong lai do WATCHDOG reset\n", millis());
      }
      node2_state = rx.data[0];
      node2_flags = rx.data[3];
    } else if (rx.identifier == ID_UDS_RESP && rx.data_length_code >= 1) {
      handle_uds_frame(rx);
    } else if (rx.identifier == ID_LED_STATUS && rx.data_length_code >= 1) {
      Serial.printf("   >>> STM32 bao: LED %s\n", rx.data[0] ? "ON" : "OFF");
    }
  }

  if (node2_alive && millis() - node2_last_ms > NODE2_TIMEOUT_MS) {
    node2_alive = false;
    node2_state = 255;
    node2_flags = 0;
    Serial.printf("[t=%lu ms] MAT STM32: %d ms khong co 0x210\n", millis(), NODE2_TIMEOUT_MS);
  }
}

/* ================= UDS tester + ISO-TP nhan ================= */
uint8_t  uds_buf[64];
uint32_t uds_len = 0, uds_pos = 0;
uint8_t  uds_sn = 0;
int      uds_frames = 0;
bool     uds_waiting = false;        /* da gui request, dang cho phan hoi */
bool     uds_receiving = false;      /* dang ghep phan hoi nhieu frame */
unsigned long uds_deadline = 0;
unsigned long uds_t0 = 0;

void uds_send_request(const uint8_t *req, uint8_t n)
{
  twai_message_t msg = {};
  msg.identifier = ID_UDS_REQ;
  msg.data_length_code = 8;
  msg.data[0] = n;                                 /* Single Frame: PCI 0x0N */
  for (int i = 0; i < 7; i++) {
    msg.data[1 + i] = (i < n) ? req[i] : 0xAA;     /* padding */
  }
  Serial.print(">> UDS request:");
  for (int i = 0; i < n; i++) {
    Serial.printf(" %02X", req[i]);
  }
  Serial.println();
  if (twai_transmit(&msg, 0) != ESP_OK) {
    Serial.println("   LOI: khong gui duoc (hang doi day / bus-off)");
    return;
  }
  uds_waiting = true;
  uds_receiving = false;
  uds_deadline = millis() + 1000;
  uds_t0 = micros();
}

const char *nrc_name(uint8_t nrc)
{
  switch (nrc) {
  case 0x11: return "serviceNotSupported";
  case 0x12: return "subFunctionNotSupported";
  case 0x13: return "incorrectMessageLengthOrInvalidFormat";
  case 0x22: return "conditionsNotCorrect";
  case 0x31: return "requestOutOfRange";
  default:   return "?";
  }
}

/* Ten DTC cua ECU nay (bang tra cuu phia tester) */
const char *dtc_desc(uint32_t code)
{
  switch (code) {
  case 0xC10087: return "mat frame 0x110 tu ESP32 (timeout)";
  case 0xC10082: return "alive counter 0x110 sai (lap / nhay)";
  case 0xC10083: return "CRC-8 E2E cua 0x110 sai";
  case 0xC00188: return "bus CAN bi bus-off";
  case 0x060600: return "loi bo xu ly ECU (watchdog reset)";
  default:       return "?";
  }
}

/* 0xC10087 -> "U0100-87" */
void print_dtc_code(uint32_t code)
{
  uint8_t b0 = code >> 16, b1 = code >> 8, ftb = code;
  Serial.printf("%c%X%X%X%X-%02X", "PCBU"[b0 >> 6], (b0 >> 4) & 0x3, b0 & 0xF, b1 >> 4, b1 & 0xF, ftb);
}

void uds_print_response()
{
  unsigned long dt = micros() - uds_t0;
  Serial.printf("<< UDS response %lu byte, %d frame CAN, %lu us:", uds_len, uds_frames, dt);
  for (uint32_t i = 0; i < uds_len; i++) {
    Serial.printf(" %02X", uds_buf[i]);
  }
  Serial.println();

  uint8_t *r = uds_buf;
  if (r[0] == 0x7F && uds_len >= 3) {
    Serial.printf("   Phan hoi AM cho SID 0x%02X: NRC 0x%02X %s\n", r[1], r[2], nrc_name(r[2]));
  } else if (r[0] == 0x59 && r[1] == 0x01 && uds_len >= 6) {
    Serial.printf("   So DTC: %u (mask ho tro 0x%02X)\n", (r[4] << 8) | r[5], r[2]);
  } else if (r[0] == 0x59 && r[1] == 0x02) {
    int count = (uds_len - 3) / 4;
    Serial.printf("   %d DTC (mask ho tro 0x%02X):\n", count, r[2]);
    for (int i = 0; i < count; i++) {
      uint8_t *d = &r[3 + 4 * i];
      uint32_t code = ((uint32_t)d[0] << 16) | (d[1] << 8) | d[2];
      uint8_t st = d[3];
      Serial.print("   ");
      print_dtc_code(code);
      Serial.printf("  status 0x%02X [%s%s%s%s]  %s\n", st,
                    (st & 0x01) ? "DANG LOI " : "",
                    (st & 0x02) ? "loi-chu-ky-nay " : "",
                    (st & 0x08) ? "confirmed " : "",
                    (st & 0x20) ? "tu-lan-xoa " : "",
                    dtc_desc(code));
    }
  } else if (r[0] == 0x54) {
    Serial.println("   Da xoa DTC");
  } else if (r[0] == 0x7E) {
    Serial.println("   TesterPresent OK");
  }
}

void send_flow_control()
{
  twai_message_t fc = {};
  fc.identifier = ID_UDS_REQ;
  fc.data_length_code = 8;
  fc.data[0] = 0x30;                               /* FC, FlowStatus 0 = ContinueToSend */
  fc.data[1] = 0x00;                               /* BlockSize 0 = gui het, khong can FC nua */
  fc.data[2] = 0x00;                               /* STmin 0 ms */
  for (int i = 3; i < 8; i++) {
    fc.data[i] = 0xAA;
  }
  twai_transmit(&fc, 0);
}

/* Moi frame 0x7E8 tu STM32 */
void handle_uds_frame(const twai_message_t &rx)
{
  const uint8_t *d = rx.data;
  uint8_t pci = d[0] >> 4;

  if (pci == 0) {                                  /* Single Frame */
    uds_len = d[0] & 0x0F;
    memcpy(uds_buf, &d[1], uds_len);
    uds_frames = 1;
    uds_waiting = false;
    uds_print_response();
  } else if (pci == 1) {                           /* First Frame */
    uds_len = ((d[0] & 0x0F) << 8) | d[1];
    if (uds_len > sizeof(uds_buf)) {
      Serial.println("   LOI ISO-TP: phan hoi qua dai");
      uds_waiting = false;
      return;
    }
    memcpy(uds_buf, &d[2], 6);
    uds_pos = 6;
    uds_sn = 1;
    uds_frames = 1;
    uds_receiving = true;
    uds_deadline = millis() + 1000;
    send_flow_control();
  } else if (pci == 2 && uds_receiving) {          /* Consecutive Frame */
    if ((d[0] & 0x0F) != uds_sn) {
      Serial.printf("   LOI ISO-TP: CF sai thu tu (mong %u, nhan %u)\n", uds_sn, d[0] & 0x0F);
      uds_receiving = false;
      uds_waiting = false;
      return;
    }
    uint32_t n = uds_len - uds_pos;
    if (n > 7) {
      n = 7;
    }
    memcpy(&uds_buf[uds_pos], &d[1], n);
    uds_pos += n;
    uds_sn = (uds_sn + 1) & 0x0F;
    uds_frames++;
    uds_deadline = millis() + 1000;
    if (uds_pos >= uds_len) {
      uds_receiving = false;
      uds_waiting = false;
      uds_print_response();
    }
  }
}

void uds_poll()
{
  if (uds_waiting && (long)(millis() - uds_deadline) >= 0) {
    Serial.println(uds_receiving ? "   LOI ISO-TP: het thoi gian cho Consecutive Frame"
                                 : "   Khong co phan hoi UDS trong 1 s");
    uds_waiting = false;
    uds_receiving = false;
  }
}

/* ================= Lenh Serial ================= */
void send_test_cmd(char cmd)
{
  twai_message_t msg = {};
  msg.identifier = ID_TEST_CMD;
  msg.data_length_code = 1;
  msg.data[0] = (uint8_t)cmd;
  bool ok = twai_transmit(&msg, 0) == ESP_OK;
  Serial.printf(">> Gui lenh '%c' cho STM32 qua 0x7F0%s\n", cmd, ok ? "" : " (LOI: hang doi day / bus-off)");
}

void handle_serial()
{
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
    case 'n': inject = INJ_NONE;   Serial.println(">> Binh thuong"); break;
    case 's': inject = INJ_SILENT; Serial.println(">> Im lang: ngung gui 0x110"); break;
    case 'f': inject = INJ_FREEZE; Serial.println(">> Dong bang: gui lai frame cu"); break;
    case 'c': inject_crc_once = true; break;
    case 'k': inject_skip_once = true; break;
    case 'i': next_print = millis(); break;
    case 'h': send_test_cmd('h'); break;
    case 'x': send_test_cmd('s'); break;
    case 'y': send_test_cmd('n'); break;
    case 'd': { const uint8_t q[] = {0x19, 0x02, 0xFF}; uds_send_request(q, 3); break; }
    case 'e': { const uint8_t q[] = {0x19, 0x01, 0xFF}; uds_send_request(q, 3); break; }
    case 'z': { const uint8_t q[] = {0x14, 0xFF, 0xFF, 0xFF}; uds_send_request(q, 4); break; }
    case 'p': { const uint8_t q[] = {0x3E, 0x00}; uds_send_request(q, 2); break; }
    case 'u': { const uint8_t q[] = {0x22, 0xF1, 0x90}; uds_send_request(q, 3); break; }
    default: break;                                /* bo qua '\r', '\n' */
    }
  }
}

void print_status()
{
  twai_status_info_t s;
  twai_get_status_info(&s);
  const char *st = (s.state == TWAI_STATE_RUNNING)    ? "RUNNING"
                 : (s.state == TWAI_STATE_BUS_OFF)    ? "BUS-OFF"
                 : (s.state == TWAI_STATE_RECOVERING) ? "RECOVERING" : "STOPPED";
  Serial.printf("[t=%lu ms] ESP32 %s TEC=%lu REC=%lu hang doi=%lu | gui=%lu loi gui=%lu "
                "loi bus=%lu busoff=%lu | STM32: %s\n",
                millis(), st, s.tx_error_counter, s.rx_error_counter, s.msgs_to_tx,
                n_sent, n_tx_fail, n_bus_err, n_busoff,
                node2_alive && node2_state < 3 ? ST_NAMES[node2_state] : "MAT");
}

void setup()
{
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  if (!mpu_write(MPU_PWR_MGMT_1, 0x00)) {
    Serial.println("Canh bao: khong thay MPU6050 (se khong gui 0x110 cho den khi doc duoc)");
  }

  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(PIN_TX, PIN_RX, TWAI_MODE_NORMAL);
  g.alerts_enabled = TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED |
                     TWAI_ALERT_ERR_ACTIVE | TWAI_ALERT_ABOVE_ERR_WARN | TWAI_ALERT_BUS_ERROR |
                     TWAI_ALERT_TX_FAILED | TWAI_ALERT_RX_QUEUE_FULL;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK || twai_start() != ESP_OK) {
    Serial.println("Loi khoi dong TWAI");
    while (1) {
      delay(1000);
    }
  }

  Serial.println("\nMoc 9: moc 8 + UDS tester (0x7E0 -> STM32, 0x7E8 <- STM32)");
  Serial.println("Lenh: n binh thuong | s im lang | f dong bang | c sai CRC | k mat 1 frame | i in");
  Serial.println("Lenh cho STM32: h treo CPU | x STM32 im lang | y STM32 gui lai");
  Serial.println("UDS: d doc DTC | e dem DTC | z xoa DTC | p TesterPresent | u service la");
  last_send = millis();
  next_print = millis() + 1000;
}

void loop()
{
  /* 1. Moi 100 ms gui 0x110 (tru khi dang gia lap im lang hoac bus-off) */
  if (millis() - last_send >= PERIOD_MS) {
    last_send += PERIOD_MS;
    if (inject != INJ_SILENT && !bus_off) {
      send_tilt();
    }
  }

  handle_rx();
  uds_poll();
  handle_alerts();
  handle_busoff_recovery();
  handle_serial();

  /* Du phong: neu controller dung ma khong phai do dang phuc hoi -> chay lai */
  twai_status_info_t s;
  twai_get_status_info(&s);
  if (s.state == TWAI_STATE_STOPPED && !bus_off) {
    twai_start();
  }

  if ((long)(millis() - next_print) >= 0) {
    next_print = millis() + 1000;
    print_status();
  }
}
