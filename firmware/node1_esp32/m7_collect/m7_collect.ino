#include <Wire.h>
#include <math.h>

#define PIN_SDA     21
#define PIN_SCL     22
#define PIN_BTN     0           /* nut BOOT tren board ESP32 */

#define MPU_ADDR        0x68
#define MPU_CONFIG      0x1A
#define MPU_GYRO_CFG    0x1B
#define MPU_ACCEL_CFG   0x1C
#define MPU_DATA        0x3B
#define MPU_PWR_MGMT_1  0x6B

#define WINDOW      100
#define PRE         30          /* PHAI giong m7_gesture_tx */
#define PERIOD_US   10000
#define GYRO_LSB    32.8f
#define TRIGGER_DPS 100.0f      /* PHAI giong m7_gesture_tx */
#define ARM_MS      3000

int16_t ring[WINDOW][6];
int head = 0;

enum { WAIT_BUTTON, ARMED, CAPTURING } state = WAIT_BUTTON;
unsigned long armed_until = 0;
int remaining = 0;
unsigned long next_us = 0;
unsigned long last_err_ms = 0;

/* ================= MPU6050 ================= */
bool mpu_write(uint8_t reg, uint8_t value)
{
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool mpu_read(uint8_t reg, uint8_t *b, uint8_t len)
{
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom((uint8_t)MPU_ADDR, len) != len) {
    return false;
  }
  for (uint8_t i = 0; i < len; i++) {
    b[i] = Wire.read();
  }
  return true;
}

void mpu_init()
{
  mpu_write(MPU_PWR_MGMT_1, 0x00);
  mpu_write(MPU_CONFIG, 0x03);
  mpu_write(MPU_GYRO_CFG, 0x10);
  mpu_write(MPU_ACCEL_CFG, 0x08);
}

bool read_imu(int16_t *v)
{
  uint8_t b[14];
  if (!mpu_read(MPU_DATA, b, 14)) {
    return false;
  }
  v[0] = (int16_t)((b[0]  << 8) | b[1]);
  v[1] = (int16_t)((b[2]  << 8) | b[3]);
  v[2] = (int16_t)((b[4]  << 8) | b[5]);
  v[3] = (int16_t)((b[8]  << 8) | b[9]);
  v[4] = (int16_t)((b[10] << 8) | b[11]);
  v[5] = (int16_t)((b[12] << 8) | b[13]);
  return true;
}

/* In cua so tu cu den moi. head dang tro vao mau cu nhat. */
void send_window()
{
  Serial.println("S");
  for (int i = 0; i < WINDOW; i++) {
    int16_t *v = ring[(head + i) % WINDOW];
    Serial.printf("%d,%d,%d,%d,%d,%d\n", v[0], v[1], v[2], v[3], v[4], v[5]);
  }
  Serial.println("E");
}

void setup()
{
  Serial.begin(115200);
  pinMode(PIN_BTN, INPUT_PULLUP);
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  mpu_init();
  Serial.println("# READY: nhan BOOT, roi lam cu chi trong 3 giay");
  next_us = micros();
}

void loop()
{
  /* Nut chi co tac dung khi dang cho */
  if (state == WAIT_BUTTON && digitalRead(PIN_BTN) == LOW) {
    delay(20);
    if (digitalRead(PIN_BTN) == LOW) {
      while (digitalRead(PIN_BTN) == LOW) {
        delay(5);
      }
      state = ARMED;
      armed_until = millis() + ARM_MS;
      Serial.println("# ARMED: lam cu chi");
      next_us = micros();
    }
  }

  if ((long)(micros() - next_us) < 0) {
    return;
  }
  next_us += PERIOD_US;

  int16_t v[6];
  bool ok = read_imu(v);
  if (!ok || (v[0] == 0 && v[1] == 0 && v[2] == 0)) {
    if (millis() - last_err_ms > 1000) {
      Serial.println("# LOI: khong doc duoc MPU6050 / tra ve 0. Mau nay bi huy");
      last_err_ms = millis();
    }
    mpu_init();
    state = WAIT_BUTTON;
    return;
  }

  memcpy(ring[head], v, sizeof(v));
  head = (head + 1) % WINDOW;
  float gdps = sqrtf((float)v[3] * v[3] + (float)v[4] * v[4] + (float)v[5] * v[5]) / GYRO_LSB;

  if (state == ARMED) {
    if (gdps > TRIGGER_DPS) {
      state = CAPTURING;
      remaining = WINDOW - PRE;
    } else if (millis() > armed_until) {
      Serial.println("# Het 3 giay ma chua thay chuyen dong > 100 do/s. Nhan BOOT lai");
      state = WAIT_BUTTON;
    }
  } else if (state == CAPTURING) {
    if (--remaining == 0) {
      send_window();
      state = WAIT_BUTTON;
    }
  }
}
