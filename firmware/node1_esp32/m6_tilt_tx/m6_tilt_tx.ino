/*
 * Moc 6 - phia ESP32: doc MPU6050, tinh goc nghieng, gui qua CAN moi 100 ms
 *
 * MPU6050 (GY-521) qua I2C: SDA = GPIO21, SCL = GPIO22, VCC = 3V3, AD0 bo trong -> dia chi 0x68
 * TWAI: G25 = TX -> CTX, G26 = RX <- CRX (module B), 500 kbit/s
 *
 * Frame gui: ID 0x110, DLC 4
 *   byte 0..1 = roll  (int16, don vi 0.01 do, little-endian: byte thap truoc)
 *   byte 2..3 = pitch (int16, don vi 0.01 do, little-endian)
 *   Vi du: roll = -12.50 do -> -1250 = 0xFB1E -> byte0 = 0x1E, byte1 = 0xFB
 *
 * Nhan: ID 0x200 tu STM32 khi LED doi trang thai (nghieng qua 30 do)
 */

#include <Wire.h>
#include <math.h>
#include "driver/twai.h"

#define PIN_TX      GPIO_NUM_25
#define PIN_RX      GPIO_NUM_26
#define PIN_SDA     21
#define PIN_SCL     22

#define MPU_ADDR        0x68
#define MPU_PWR_MGMT_1  0x6B    /* ghi 0 de danh thuc chip (mac dinh dang ngu) */
#define MPU_ACCEL_XOUT  0x3B    /* 6 byte: AX_H AX_L AY_H AY_L AZ_H AZ_L */
#define MPU_WHO_AM_I    0x75

#define ID_TILT         0x110
#define ID_LED_STATUS   0x200
#define PERIOD_MS       100

unsigned long last_send = 0;
uint32_t frame_count = 0;

/* ================= MPU6050 ================= */
bool mpu_write(uint8_t reg, uint8_t value)
{
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;      /* 0 = co ACK tu MPU6050 */
}

bool mpu_read(uint8_t reg, uint8_t *buf, uint8_t len)
{
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);                         /* chon thanh ghi bat dau */
  if (Wire.endTransmission(false) != 0) {  /* false: repeated start, chua nha bus */
    return false;
  }
  if (Wire.requestFrom((uint8_t)MPU_ADDR, len) != len) {
    return false;
  }
  for (uint8_t i = 0; i < len; i++) {
    buf[i] = Wire.read();
  }
  return true;
}

/* Doc gia toc 3 truc (don vi tho, +-2g -> 16384 = 1g) */
bool mpu_read_accel(int16_t *ax, int16_t *ay, int16_t *az)
{
  uint8_t b[6];
  if (!mpu_read(MPU_ACCEL_XOUT, b, 6)) {
    return false;
  }
  /* MPU6050 gui byte cao truoc (big-endian) */
  *ax = (int16_t)((b[0] << 8) | b[1]);
  *ay = (int16_t)((b[2] << 8) | b[3]);
  *az = (int16_t)((b[4] << 8) | b[5]);
  return true;
}

/* ================= CAN ================= */
void send_tilt(int16_t roll, int16_t pitch)
{
  twai_message_t msg = {};
  msg.identifier = ID_TILT;
  msg.data_length_code = 4;
  msg.data[0] = (uint8_t)(roll & 0xFF);          /* byte thap */
  msg.data[1] = (uint8_t)((roll >> 8) & 0xFF);   /* byte cao */
  msg.data[2] = (uint8_t)(pitch & 0xFF);
  msg.data[3] = (uint8_t)((pitch >> 8) & 0xFF);

  if (twai_transmit(&msg, 0) != ESP_OK) {
    Serial.println("Hang doi TX day");
  }
}

void setup()
{
  Serial.begin(115200);

  /* I2C 400 kHz */
  Wire.begin(PIN_SDA, PIN_SCL, 400000);

  uint8_t who = 0;
  if (!mpu_read(MPU_WHO_AM_I, &who, 1)) {
    Serial.println("Khong thay MPU6050 o 0x68 (kiem tra SDA/SCL/VCC/GND)");
    while (1) {
      delay(1000);
    }
  }
  /* Chip that tra ve 0x68; mot so ban clone tra 0x70, 0x72, 0x98 nhung van doc duoc */
  Serial.printf("\nMPU6050 WHO_AM_I = 0x%02X\n", who);
  mpu_write(MPU_PWR_MGMT_1, 0x00);         /* danh thuc */

  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(PIN_TX, PIN_RX, TWAI_MODE_NORMAL);
  twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK || twai_start() != ESP_OK) {
    Serial.println("Loi khoi dong TWAI");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("Moc 6: gui goc nghieng 0x110 moi 100 ms. Nghieng > 30 do -> LED Blue Pill sang");
}

void loop()
{
  /* 1. Moi 100 ms: doc cam bien, tinh goc, gui */
  if (millis() - last_send >= PERIOD_MS) {
    last_send += PERIOD_MS;                /* giu dung nhip 100 ms, khong bi troi */

    int16_t ax, ay, az;
    if (mpu_read_accel(&ax, &ay, &az)) {
      float roll  = atan2f((float)ay, (float)az) * 180.0f / PI;
      float pitch = atan2f(-(float)ax, sqrtf((float)ay * ay + (float)az * az)) * 180.0f / PI;

      int16_t roll_c  = (int16_t)lroundf(roll * 100.0f);   /* 0.01 do */
      int16_t pitch_c = (int16_t)lroundf(pitch * 100.0f);
      send_tilt(roll_c, pitch_c);
      frame_count++;

      if (frame_count % 5 == 0) {          /* in 2 lan/giay cho de doc */
        twai_status_info_t s;
        twai_get_status_info(&s);
        Serial.printf("roll=%7.2f pitch=%7.2f | TEC=%lu loi bus=%lu\n",
                      roll, pitch, s.tx_error_counter, s.bus_error_count);
      }
    } else {
      Serial.println("Loi doc MPU6050");
    }
  }

  /* 2. Nhan trang thai LED tu STM32 */
  twai_message_t rx;
  while (twai_receive(&rx, 0) == ESP_OK) {
    if (rx.identifier == ID_LED_STATUS && rx.data_length_code >= 1) {
      Serial.printf("   >>> STM32 bao: LED %s\n", rx.data[0] ? "ON (nghieng)" : "OFF (thang)");
    }
  }

  /* 3. Bus-off -> phuc hoi */
  twai_status_info_t s;
  twai_get_status_info(&s);
  if (s.state == TWAI_STATE_BUS_OFF) {
    twai_initiate_recovery();
  } else if (s.state == TWAI_STATE_STOPPED) {
    twai_start();
  }
}
