/*
 * Moc 3 - phia ESP32: gui frame CAN qua bus that toi Blue Pill
 *
 * - TWAI_MODE_NORMAL: frame phai duoc node khac (Blue Pill) tra ACK
 * - GPIO25 (TX) -> CTX cua SN65HVD230 #2
 * - GPIO26 (RX) <- CRX cua SN65HVD230 #2
 * - 500 kbit/s, gui ID 0x123 moi giay
 */

#include "driver/twai.h"

#define CAN_TX_PIN  GPIO_NUM_25
#define CAN_RX_PIN  GPIO_NUM_26

uint8_t counter = 0;

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

    /* Khac moc 2: NORMAL thay vi NO_ACK */
    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK ||
        twai_start() != ESP_OK) {
        Serial.println("Loi: khoi dong TWAI that bai");
        while (1) {
        }
    }
    Serial.println("ESP32 - moc 3: gui CAN 500 kbit/s (NORMAL)");
}

void loop()
{
    twai_status_info_t st;
    twai_get_status_info(&st);

    if (st.state == TWAI_STATE_BUS_OFF) {
        Serial.println(">>> BUS-OFF! Phuc hoi...");
        twai_initiate_recovery();
        delay(500);
        return;
    }
    if (st.state == TWAI_STATE_STOPPED) {
        twai_start();
    }

    twai_message_t tx_msg = {};
    tx_msg.identifier = 0x123;
    tx_msg.data_length_code = 2;
    tx_msg.data[0] = 0xAB;
    tx_msg.data[1] = counter;

    if (twai_transmit(&tx_msg, pdMS_TO_TICKS(100)) == ESP_OK) {
        Serial.printf("Dua vao hang doi: data=AB %02X\n", counter);
    } else {
        Serial.println("Hang doi gui DAY");
    }

    delay(50);
    twai_get_status_info(&st);
    Serial.printf("   [%s] TEC=%lu REC=%lu | cho gui=%lu | loi bus=%lu\n\n",
                  state_name(st.state),
                  (unsigned long)st.tx_error_counter,
                  (unsigned long)st.rx_error_counter,
                  (unsigned long)st.msgs_to_tx,
                  (unsigned long)st.bus_error_count);

    counter++;
    delay(1000);
}
