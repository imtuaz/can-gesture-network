/*
 * Moc 7 - buoc 1: thu du lieu cu chi
 *
 * Moi lan nhan nut: ghi 1 cua so 1 giay (100 mau x 6 truc, 100 Hz)
 * roi gui qua Serial cho script collect.py luu vao CSV.
 *
 * MPU6050 (GY-521): SDA = G21, SCL = G22, VCC = 3V3, GND = GND
 * Nut: BOOT tren board (GPIO0). Khong giu BOOT luc bam EN, ESP32 se vao che do nap.
 *
 * Dinh dang Serial:
 *   S                      <- bat dau 1 mau
 *   ax,ay,az,gx,gy,gz      <- 100 dong, so nguyen tho tu cam bien
 *   E                      <- ket thuc mau
 *   # ...                  <- dong thong bao, script chi in ra man hinh
 */

#include <Wire.h>

#define PIN_SDA     21
#define PIN_SCL     22
#define PIN_BTN     0           /* nut BOOT tren board ESP32 */

#define MPU_ADDR        0x68
#define MPU_SMPLRT_DIV  0x19
#define MPU_CONFIG      0x1A    /* loc thong thap (DLPF) */
#define MPU_GYRO_CFG    0x1B
#define MPU_ACCEL_CFG   0x1C
#define MPU_DATA        0x3B    /* 14 byte: accel(6) temp(2) gyro(6) */
#define MPU_PWR_MGMT_1  0x6B

#define WINDOW      100         /* 100 mau = 1 giay */
#define PERIOD_US   10000       /* 10 ms -> 100 Hz */

int16_t buf[WINDOW][6];

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
  mpu_write(MPU_PWR_MGMT_1, 0x00);   /* danh thuc */
  mpu_write(MPU_CONFIG, 0x03);       /* DLPF ~44 Hz: bo nhieu cao tan, du cho lay mau 100 Hz */
  mpu_write(MPU_GYRO_CFG, 0x10);     /* +-1000 do/s -> 32.8 LSB/(do/s). Hat co tay nhanh de vuot 500 */
  mpu_write(MPU_ACCEL_CFG, 0x08);    /* +-4g -> 8192 LSB/g. Hat manh de vuot 2g */
}

/* Doc 1 mau 6 truc. v[0..2] = ax ay az, v[3..5] = gx gy gz */
bool read_imu(int16_t *v)
{
  uint8_t b[14];
  if (!mpu_read(MPU_DATA, b, 14)) {
    return false;
  }
  v[0] = (int16_t)((b[0]  << 8) | b[1]);
  v[1] = (int16_t)((b[2]  << 8) | b[3]);
  v[2] = (int16_t)((b[4]  << 8) | b[5]);
  /* b[6], b[7] la nhiet do: bo qua */
  v[3] = (int16_t)((b[8]  << 8) | b[9]);
  v[4] = (int16_t)((b[10] << 8) | b[11]);
  v[5] = (int16_t)((b[12] << 8) | b[13]);
  return true;
}

/* ================= Ghi 1 cua so ================= */
bool record_window()
{
  unsigned long t = micros();
  for (int i = 0; i < WINDOW; i++) {
    while (micros() - t < PERIOD_US) {
    }                                  /* cho dung moc 10 ms */
    t += PERIOD_US;
    if (!read_imu(buf[i])) {
      return false;
    }
  }
  return true;
}

/* Cam bien bi reset se ngu va tra ve toan so 0 (loi gap o moc 6) */
bool window_all_zero()
{
  for (int i = 0; i < WINDOW; i++) {
    for (int k = 0; k < 6; k++) {
      if (buf[i][k] != 0) {
        return false;
      }
    }
  }
  return true;
}

void setup()
{
  Serial.begin(115200);
  pinMode(PIN_BTN, INPUT_PULLUP);
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  mpu_init();
  Serial.println("# READY: nhan nut roi lam cu chi trong 1 giay");
}

void loop()
{
  if (digitalRead(PIN_BTN) != LOW) {
    return;
  }
  delay(20);                           /* chong doi */
  if (digitalRead(PIN_BTN) != LOW) {
    return;
  }

  bool ok = record_window();           /* ghi xong moi in, de Serial khong lam lech nhip 100 Hz */

  if (!ok) {
    Serial.println("# LOI: doc MPU6050 that bai, kiem tra day");
  } else if (window_all_zero()) {
    Serial.println("# LOI: MPU6050 tra ve toan 0 (bi reset), da danh thuc lai. Thu lai mau nay");
    mpu_init();
  } else {
    Serial.println("S");
    for (int i = 0; i < WINDOW; i++) {
      Serial.printf("%d,%d,%d,%d,%d,%d\n",
                    buf[i][0], buf[i][1], buf[i][2], buf[i][3], buf[i][4], buf[i][5]);
    }
    Serial.println("E");
  }

  while (digitalRead(PIN_BTN) == LOW) {  /* cho nha nut, moi lan nhan = 1 mau */
    delay(10);
  }
}
