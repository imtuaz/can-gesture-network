/*
 * Test e2e.h tren PC:  gcc -std=c99 -Wall -Wextra -I firmware/common tests/test_e2e.c -o test_e2e && ./test_e2e
 */
#include <stdio.h>
#include <string.h>
#include "e2e.h"

static int fails = 0;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL dong %d: %s\n", __LINE__, #cond); fails++; } \
} while (0)

/* Tao 1 frame 0x110 (roll, pitch) da bao ve */
static void make_frame(uint8_t *d, int16_t roll, int16_t pitch, uint8_t *counter)
{
    d[0] = (uint8_t)roll;
    d[1] = (uint8_t)((uint16_t)roll >> 8);
    d[2] = (uint8_t)pitch;
    d[3] = (uint8_t)((uint16_t)pitch >> 8);
    e2e_protect(0x110, d, 4, counter);
}

int main(void)
{
    uint8_t d[8];
    uint8_t tx_cnt = 0;
    uint32_t lost;
    e2e_rx_t rx = {0, 0};

    /* 1. Gia tri kiem tra chuan cua CRC-8 SAE J1850 */
    CHECK(crc8_j1850((const uint8_t *)"123456789", 9) == 0x4B);

    /* 2. Chuoi frame binh thuong, ke ca luc counter quay vong 15 -> 0 */
    for (int i = 0; i < 40; i++) {
        make_frame(d, (int16_t)(i * 10), -5, &tx_cnt);
        CHECK(e2e_check(&rx, 0x110, d, 6, 4, &lost) == E2E_OK);
    }

    /* 3. Lat 1 bit du lieu -> CRC sai */
    make_frame(d, 100, 200, &tx_cnt);
    d[1] ^= 0x04;
    CHECK(e2e_check(&rx, 0x110, d, 6, 4, &lost) == E2E_ERR_CRC);

    /* 4. Frame dung nhung gan nham ID 0x111 -> CRC sai (vi CRC tinh ca ID) */
    make_frame(d, 100, 200, &tx_cnt);
    CHECK(e2e_check(&rx, 0x111, d, 6, 4, &lost) == E2E_ERR_CRC);
    /* Dua lai frame buoc 4 voi dung ID: frame buoc 3 bi bo -> ben nhan thay mat 1 frame */
    CHECK(e2e_check(&rx, 0x110, d, 6, 4, &lost) == E2E_OK_LOST && lost == 1);

    /* 5. Gui lai dung frame cu (ben gui bi treo) -> REPEATED */
    CHECK(e2e_check(&rx, 0x110, d, 6, 4, &lost) == E2E_ERR_REPEATED);

    /* 6. Mat 1 frame */
    make_frame(d, 1, 1, &tx_cnt);           /* frame nay "bi mat", khong dua vao */
    make_frame(d, 2, 2, &tx_cnt);
    CHECK(e2e_check(&rx, 0x110, d, 6, 4, &lost) == E2E_OK_LOST && lost == 1);

    /* 7. Mat 5 frame -> SEQUENCE, sau do dong bo lai */
    for (int i = 0; i < 6; i++) {
        make_frame(d, 3, 3, &tx_cnt);
    }
    CHECK(e2e_check(&rx, 0x110, d, 6, 4, &lost) == E2E_ERR_SEQUENCE);
    make_frame(d, 4, 4, &tx_cnt);
    CHECK(e2e_check(&rx, 0x110, d, 6, 4, &lost) == E2E_OK);

    /* 8. DLC 4 (frame kieu moc 6, chua co E2E) -> LEN */
    CHECK(e2e_check(&rx, 0x110, d, 4, 4, &lost) == E2E_ERR_LEN);

    /* 9. Thu lat tung bit trong 6 byte: CRC-8 phai bat duoc moi loi 1 bit */
    make_frame(d, -1234, 567, &tx_cnt);
    int missed = 0;
    for (int byte = 0; byte < 6; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            uint8_t c[8];
            e2e_rx_t r = rx;
            memcpy(c, d, 8);
            c[byte] ^= (uint8_t)(1u << bit);
            if (e2e_check(&r, 0x110, c, 6, 4, &lost) != E2E_ERR_CRC) {
                missed++;
            }
        }
    }
    CHECK(missed == 0);

    printf("%s (%d loi)\n", fails == 0 ? "PASS" : "FAIL", fails);
    return fails != 0;
}
