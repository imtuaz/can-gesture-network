#include <Wire.h>
#include <math.h>
#include "driver/twai.h"
#include "gesture_model.h"

#if N_FEAT != 48
#error "gesture_model.h la ban cu (24 dac trung). Chay lai train.py de tao ban 48 dac trung"
#endif

#define PIN_TX      GPIO_NUM_25
#define PIN_RX      GPIO_NUM_26
#define PIN_SDA     21
#define PIN_SCL     22

#define MPU_ADDR        0x68
#define MPU_CONFIG      0x1A
#define MPU_GYRO_CFG    0x1B
#define MPU_ACCEL_CFG   0x1C
#define MPU_DATA        0x3B
#define MPU_PWR_MGMT_1  0x6B

#define WINDOW          100     /* 1 giay */
#define PRE             30      /* so mau giu lai truoc luc bat dau cu chi */
#define PERIOD_US       10000   /* 100 Hz */
#define GYRO_LSB        32.8f   /* LSB moi do/s o dai +-1000 */
#define TRIGGER_DPS     100.0f
#define COOLDOWN_MS     500
#define MIN_PROB        0.70f

#define ID_GESTURE      0x120
#define ID_LED_STATUS   0x200

const char *NAMES[N_CLASS] = {"idle", "right", "left", "rotate"};

int16_t ring[WINDOW][6];        /* bo dem vong: 100 mau gan nhat */
int head = 0;                   /* vi tri se ghi mau tiep theo */
int16_t win[WINDOW][6];         /* cua so da sap xep cu -> moi, dua vao model */

bool capturing = false;
int remaining = 0;
unsigned long cooldown_until = 0;
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

/* ================= Mang no-ron ================= */
/* Tinh mean, std, min, max cua 6 truc tren doan [from, from + n) -> out[24]
 * Thu tu: out[0..5] mean, out[6..11] std, out[12..17] min, out[18..23] max */
void stats(int16_t w[WINDOW][6], int from, int n, float *out)
{
  for (int k = 0; k < 6; k++) {
    float sum = 0, mn = w[from][k], mx = w[from][k];
    for (int i = from; i < from + n; i++) {
      float x = w[i][k];
      sum += x;
      if (x < mn) mn = x;
      if (x > mx) mx = x;
    }
    float mean = sum / n;
    float var = 0;
    for (int i = from; i < from + n; i++) {
      float d = w[i][k] - mean;
      var += d * d;
    }
    out[k]      = mean;
    out[6 + k]  = sqrtf(var / n);      /* chia N, giong numpy std */
    out[12 + k] = mn;
    out[18 + k] = mx;
  }
}

/* 48 dac trung: 24 cua nua dau + 24 cua nua sau. Thu tu phai giong train.py */
void compute_features(int16_t w[WINDOW][6], float *f)
{
  stats(w, 0, WINDOW / 2, f);
  stats(w, WINDOW / 2, WINDOW / 2, f + 24);
}

/* Tinh logits (diem cua 4 lop) tu 24 dac trung */
void forward(const float *feat, float *logits)
{
  float x[N_FEAT];
  float h[N_HID];

  for (int i = 0; i < N_FEAT; i++) {              /* chuan hoa */
    x[i] = (feat[i] - FEAT_MEAN[i]) / FEAT_SCALE[i];
  }
  for (int j = 0; j < N_HID; j++) {               /* lop an: h = ReLU(x * NN_W1 + NN_B1) */
    float s = NN_B1[j];
    for (int i = 0; i < N_FEAT; i++) {
      s += x[i] * NN_W1[i][j];
    }
    h[j] = (s > 0) ? s : 0;
  }
  for (int c = 0; c < N_CLASS; c++) {             /* lop ra: logits = h * NN_W2 + NN_B2 */
    float s = NN_B2[c];
    for (int j = 0; j < N_HID; j++) {
      s += h[j] * NN_W2[j][c];
    }
    logits[c] = s;
  }
}

/* Softmax: doi diem thanh xac suat, tra ve lop lon nhat */
int classify(const float *logits, float *prob)
{
  int best = 0;
  for (int c = 1; c < N_CLASS; c++) {
    if (logits[c] > logits[best]) best = c;
  }
  float sum = 0;
  float p[N_CLASS];
  for (int c = 0; c < N_CLASS; c++) {
    p[c] = expf(logits[c] - logits[best]);        /* tru max de exp khong tran so */
    sum += p[c];
  }
  *prob = p[best] / sum;
  return best;
}

/* So sanh ket qua C voi Python tren cung 1 mau (golden vector) */
void self_test()
{
  float logits[N_CLASS];
  forward(TEST_FEAT, logits);
  float err = 0;
  for (int c = 0; c < N_CLASS; c++) {
    err = fmaxf(err, fabsf(logits[c] - TEST_LOGITS[c]));
  }
  Serial.printf("Self-test model: sai lech lon nhat = %.6f -> %s\n",
                err, err < 1e-3f ? "PASS" : "FAIL (kiem tra gesture_model.h)");
}

/* ================= CAN ================= */
void send_gesture(int id, float prob)
{
  twai_message_t msg = {};
  msg.identifier = ID_GESTURE;
  msg.data_length_code = 2;
  msg.data[0] = (uint8_t)id;
  msg.data[1] = (uint8_t)(prob * 100.0f + 0.5f);
  if (twai_transmit(&msg, 0) != ESP_OK) {
    Serial.println("Hang doi TX day");
  }
}

void run_model()
{
  /* Sap xep bo dem vong thanh cua so tu cu den moi.
   * head dang tro vao mau cu nhat (sap bi ghi de). */
  for (int i = 0; i < WINDOW; i++) {
    memcpy(win[i], ring[(head + i) % WINDOW], sizeof(win[i]));
  }

  unsigned long t0 = micros();
  float feat[N_FEAT];
  float logits[N_CLASS];
  float prob;
  compute_features(win, feat);
  forward(feat, logits);
  int g = classify(logits, &prob);
  unsigned long dt = micros() - t0;

  Serial.printf("Cu chi: %-6s  xac suat %3.0f%%  (tinh mat %lu us)", NAMES[g], prob * 100, dt);
  if (g != 0 && prob >= MIN_PROB) {
    send_gesture(g, prob);
    Serial.println("  -> gui 0x120");
  } else {
    Serial.println("  -> bo qua");
  }
}

void setup()
{
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  mpu_init();

  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(PIN_TX, PIN_RX, TWAI_MODE_NORMAL);
  twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK || twai_start() != ESP_OK) {
    Serial.println("Loi khoi dong TWAI");
    while (1) {
      delay(1000);
    }
  }

  Serial.println("\nMoc 7: nhan dang cu chi tren ESP32");
  self_test();
  next_us = micros();
}

void loop()
{
  /* 1. Moi 10 ms doc 1 mau vao bo dem vong */
  if ((long)(micros() - next_us) >= 0) {
    next_us += PERIOD_US;

    int16_t v[6];
    bool ok = read_imu(v);

    /* Kiem tra hop ly: doc I2C loi, hoac ca 3 truc gia toc = 0 (trong luc 1g luon co)
     * -> MPU6050 bi mat dien / reset ve che do ngu. Bao loi va danh thuc lai. */
    if (!ok || (v[0] == 0 && v[1] == 0 && v[2] == 0)) {
      if (millis() - last_err_ms > 1000) {
        Serial.println(ok ? "LOI: MPU6050 tra ve 0 (bi reset), danh thuc lai"
                          : "LOI: khong doc duoc MPU6050 (kiem tra day SDA/SCL/VCC/GND)");
        last_err_ms = millis();
      }
      mpu_init();
      capturing = false;                 /* bo cu chi dang ghi do du lieu hong */
      ok = false;
    }

    if (ok) {
      memcpy(ring[head], v, sizeof(v));
      head = (head + 1) % WINDOW;

      float gdps = sqrtf((float)v[3] * v[3] + (float)v[4] * v[4] + (float)v[5] * v[5]) / GYRO_LSB;

      if (!capturing) {
        if (millis() >= cooldown_until && gdps > TRIGGER_DPS) {
          capturing = true;              /* bat dau cu chi: da co san 30 mau truoc */
          remaining = WINDOW - PRE;
        }
      } else if (--remaining == 0) {
        capturing = false;
        run_model();
        cooldown_until = millis() + COOLDOWN_MS;
      }
    }
  }

  /* 2. Nhan phan hoi tu STM32 */
  twai_message_t rx;
  while (twai_receive(&rx, 0) == ESP_OK) {
    if (rx.identifier == ID_LED_STATUS && rx.data_length_code >= 1) {
      Serial.printf("   >>> STM32 bao: LED %s\n", rx.data[0] ? "ON" : "OFF");
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
