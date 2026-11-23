/*
 * Moc 5 - phia ESP32: nut nhan -> lenh LED qua CAN, nhan trang thai tu STM32
 *
 * - Nut: BOOT tren board (GPIO0), nhan = muc 0
 * - Moi lan nhan: doi lenh LED (0 <-> 1), gui ID 0x100, DLC 1
 * - Nhan ID 0x200 tu STM32: in trang thai LED STM32 bao lai
 * - Sau 100 ms khong thay 0x200 -> bao "STM32 khong tra loi"
 *
 * TWAI: G25 = TX -> CTX, G26 = RX <- CRX (module B), 500 kbit/s
 */

#include "driver/twai.h"

#define PIN_TX      GPIO_NUM_25
#define PIN_RX      GPIO_NUM_26
#define PIN_BUTTON  0             /* nut BOOT */

#define ID_LED_CMD      0x100
#define ID_LED_STATUS   0x200

#define DEBOUNCE_MS     30
#define REPLY_TIMEOUT   100       /* ms */

uint8_t led_cmd = 0;              /* lenh LED da gui gan nhat */

/* Chong doi nut */
int last_read = HIGH;             /* lan doc truoc */
int stable = HIGH;                /* trang thai da on dinh */
unsigned long last_change = 0;    /* thoi diem lan doc thay doi */

/* Cho tra loi */
bool waiting_reply = false;
unsigned long sent_time = 0;

void print_status()
{
  twai_status_info_t s;
  twai_get_status_info(&s);
  Serial.printf("   TEC=%lu REC=%lu | cho gui=%lu | loi bus=%lu\n",
                s.tx_error_counter, s.rx_error_counter,
                s.msgs_to_tx, s.bus_error_count);
}

void send_led_cmd()
{
  twai_message_t msg = {};
  msg.identifier = ID_LED_CMD;
  msg.data_length_code = 1;
  msg.data[0] = led_cmd;

  if (twai_transmit(&msg, pdMS_TO_TICKS(10)) == ESP_OK) {
    Serial.printf("Gui 0x100: LED %s\n", led_cmd ? "ON" : "OFF");
    waiting_reply = true;
    sent_time = millis();
  } else {
    Serial.println("Hang doi TX day, khong gui duoc");
  }
}

/* Tra ve true dung 1 lan moi khi nut vua duoc nhan xuong */
bool button_pressed()
{
  int r = digitalRead(PIN_BUTTON);

  if (r != last_read) {           /* tin hieu vua doi -> bat dau dem lai */
    last_read = r;
    last_change = millis();
  }

  if (millis() - last_change > DEBOUNCE_MS && r != stable) {
    stable = r;                   /* da giu yen du lau -> chap nhan */
    if (stable == LOW) {
      return true;                /* canh nhan xuong */
    }
  }
  return false;
}

void setup()
{
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(PIN_TX, PIN_RX, TWAI_MODE_NORMAL);
  twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g, &t, &f) != ESP_OK || twai_start() != ESP_OK) {
    Serial.println("Loi khoi dong TWAI");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("\nMoc 5: nhan nut BOOT de bat/tat LED tren Blue Pill");
}

void loop()
{
  /* 1. Nut nhan -> doi lenh va gui */
  if (button_pressed()) {
    led_cmd = !led_cmd;
    send_led_cmd();
  }

  /* 2. Lay het frame dang co trong hang doi RX (khong cho) */
  twai_message_t rx;
  while (twai_receive(&rx, 0) == ESP_OK) {
    if (rx.identifier == ID_LED_STATUS && rx.data_length_code >= 1) {
      unsigned long dt = millis() - sent_time;
      Serial.printf("   STM32 bao: LED %s (sau %lu ms)\n",
                    rx.data[0] ? "ON" : "OFF", dt);
      if (rx.data[0] != led_cmd) {
        Serial.println("   !! Trang thai khong khop lenh");
      }
      waiting_reply = false;
      print_status();
    }
  }

  /* 3. Qua thoi gian ma chua co tra loi */
  if (waiting_reply && millis() - sent_time > REPLY_TIMEOUT) {
    Serial.println("   STM32 khong tra loi (kiem tra da nap code moc 5 chua)");
    waiting_reply = false;
    print_status();
  }

  /* 4. Bus-off -> phuc hoi (giong moc 3) */
  twai_status_info_t s;
  twai_get_status_info(&s);
  if (s.state == TWAI_STATE_BUS_OFF) {
    Serial.println("BUS-OFF, dang phuc hoi...");
    twai_initiate_recovery();
  } else if (s.state == TWAI_STATE_STOPPED) {
    twai_start();
  }
}
