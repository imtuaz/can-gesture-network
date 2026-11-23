/*
 * Moc 2b: ESP32 TWAI loopback + theo doi loi bus
 *
 * Giong moc 2, them:
 * - Doc HET frame trong hang doi nhan moi vong (khong con bi tre)
 * - In trang thai bo dieu khien: TEC, REC, so frame dang cho, so loi
 * - Tu phuc hoi khi bi BUS-OFF
 *
 * Phan cung: jumper GPIO25 (TX) <-> GPIO26 (RX). Rut day ra de tao loi.
 */

#include "driver/twai.h"

#define CAN_TX_PIN  GPIO_NUM_25
#define CAN_RX_PIN  GPIO_NUM_26

uint8_t counter = 0;

/* Doi trang thai (so) thanh chu de in */
const char *state_name(twai_state_t s)
{
    switch (s) {
    case TWAI_STATE_STOPPED:    return "STOPPED";
    case TWAI_STATE_RUNNING:    return "RUNNING";
    case TWAI_STATE_BUS_OFF:    return "BUS_OFF";
    case TWAI_STATE_RECOVERING: return "RECOVERING";
    default:                    return "?";
    }
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NO_ACK);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        Serial.println("Loi: cai driver TWAI that bai");
        while (1) {
        }
    }
    if (twai_start() != ESP_OK) {
        Serial.println("Loi: khoi dong TWAI that bai");
        while (1) {
        }
    }
    Serial.println("TWAI da chay: NO_ACK, 500 kbit/s");
}

void loop()
{
    twai_status_info_t st;

    /* ---------- 1. Kiem tra trang thai truoc khi gui ---------- */
    twai_get_status_info(&st);

    if (st.state == TWAI_STATE_BUS_OFF) {
        /* Bus-off: node tu ngat khoi bus. Phai xin phuc hoi. */
        Serial.println(">>> BUS-OFF! Bat dau phuc hoi...");
        twai_initiate_recovery();
        delay(500);
        return;
    }
    if (st.state == TWAI_STATE_STOPPED) {
        /* Phuc hoi xong thi driver ve STOPPED, phai start lai */
        Serial.println(">>> Phuc hoi xong, khoi dong lai TWAI");
        twai_start();
    }

    /* ---------- 2. Gui 1 frame ---------- */
    twai_message_t tx_msg = {};
    tx_msg.identifier = 0x123;
    tx_msg.data_length_code = 2;
    tx_msg.data[0] = 0xAB;
    tx_msg.data[1] = counter;
    tx_msg.self = 1;

    esp_err_t err = twai_transmit(&tx_msg, pdMS_TO_TICKS(100));
    if (err == ESP_OK) {
        Serial.printf("Dua vao hang doi: data=AB %02X\n", counter);
    } else if (err == ESP_ERR_TIMEOUT) {
        Serial.println("Hang doi gui DAY, bo frame nay");
    } else {
        Serial.printf("Gui loi: %s\n", esp_err_to_name(err));
    }

    delay(50);   /* cho frame kip di qua bus */

    /* ---------- 3. Doc HET frame dang co trong hang doi nhan ---------- */
    twai_message_t rx_msg;
    int got = 0;
    while (twai_receive(&rx_msg, 0) == ESP_OK) {   /* timeout 0: khong cho */
        Serial.printf("   Nhan: data=AB %02X\n", rx_msg.data[1]);
        got++;
    }
    if (got == 0) {
        Serial.println("   Khong nhan duoc frame nao");
    }

    /* ---------- 4. In trang thai bo dieu khien ---------- */
    twai_get_status_info(&st);
    Serial.printf("   [%s] TEC=%lu REC=%lu | cho gui=%lu | loi bus=%lu tx_failed=%lu\n\n",
                  state_name(st.state),
                  (unsigned long)st.tx_error_counter,
                  (unsigned long)st.rx_error_counter,
                  (unsigned long)st.msgs_to_tx,
                  (unsigned long)st.bus_error_count,
                  (unsigned long)st.tx_failed_count);

    counter++;
    delay(1000);
}
