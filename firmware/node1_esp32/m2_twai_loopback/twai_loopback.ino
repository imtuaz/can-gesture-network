#include "driver/twai.h"

#define CAN_TX_PIN  GPIO_NUM_25
#define CAN_RX_PIN  GPIO_NUM_26

uint8_t counter = 0;

void setup()
{
    Serial.begin(115200);
    delay(1000);

    /* 1. Cau hinh chung: chan TX, chan RX, che do NO_ACK */
    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NO_ACK);

    /* 2. Cau hinh bit timing cho 500 kbit/s (dung bo gia tri co san) */
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();

    /* 3. Bo loc: nhan tat ca frame */
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    /* 4. Cai driver va khoi dong bo dieu khien TWAI */
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
    /* Tao frame: ID 0x123, 2 byte du lieu */
    twai_message_t tx_msg = {};
    tx_msg.identifier = 0x123;
    tx_msg.data_length_code = 2;
    tx_msg.data[0] = 0xAB;
    tx_msg.data[1] = counter;
    tx_msg.self = 1;            /* yeu cau tu nhan lai frame nay */

    /* Gui frame (cho toi da 100 ms neu hang doi gui dang day) */
    if (twai_transmit(&tx_msg, pdMS_TO_TICKS(100)) == ESP_OK) {
        Serial.printf("Gui : ID=0x%03lX DLC=%d data=%02X %02X\n",
                      (unsigned long)tx_msg.identifier, tx_msg.data_length_code,
                      tx_msg.data[0], tx_msg.data[1]);
    } else {
        Serial.println("Gui that bai");
    }

    /* Nhan frame (cho toi da 100 ms) */
    twai_message_t rx_msg;
    if (twai_receive(&rx_msg, pdMS_TO_TICKS(100)) == ESP_OK) {
        Serial.printf("Nhan: ID=0x%03lX DLC=%d data=%02X %02X\n\n",
                      (unsigned long)rx_msg.identifier, rx_msg.data_length_code,
                      rx_msg.data[0], rx_msg.data[1]);
    } else {
        Serial.println("Khong nhan duoc frame\n");
    }

    counter++;
    delay(1000);
}
