/*
 * Test uds_server.h tren PC:
 *   gcc -std=c99 -Wall -Wextra -I firmware/common tests/test_uds.c -o test_uds && ./test_uds
 */
#include <stdio.h>
#include <string.h>
#include "uds_server.h"

static int fails = 0;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL dong %d: %s\n", __LINE__, #cond); fails++; } \
} while (0)

static int same(const uint8_t *a, uint32_t na, const uint8_t *b, uint32_t nb)
{
    return na == nb && memcmp(a, b, na) == 0;
}

int main(void)
{
    uint8_t r[32];
    uint32_t n;

    /* 1. Chua co loi: 19 02 FF -> chi co header */
    const uint8_t q_list[] = {0x19, 0x02, 0xFF};
    n = uds_handle(q_list, 3, r);
    const uint8_t e_empty[] = {0x59, 0x02, 0x2B};
    CHECK(same(r, n, e_empty, 3));

    /* 2. Timeout dang co + bus-off da het: 2 DTC */
    dtc_report(DTC_LOST_COMM, 1);
    dtc_report(DTC_BUSOFF, 1);
    dtc_report(DTC_BUSOFF, 0);
    n = uds_handle(q_list, 3, r);
    const uint8_t e_two[] = {0x59, 0x02, 0x2B,
                             0xC1, 0x00, 0x87, 0x2B,    /* U0100-87, dang loi */
                             0xC0, 0x01, 0x88, 0x2A};   /* U0001-88, da het loi */
    CHECK(same(r, n, e_two, sizeof(e_two)));

    /* 3. Mask 0x01 (chi loi dang co) -> 1 DTC; dem bang sub 0x01 */
    const uint8_t q_cur[] = {0x19, 0x02, 0x01};
    n = uds_handle(q_cur, 3, r);
    CHECK(n == 7 && r[3] == 0xC1 && r[5] == 0x87);
    const uint8_t q_cnt[] = {0x19, 0x01, 0xFF};
    n = uds_handle(q_cnt, 3, r);
    const uint8_t e_cnt[] = {0x59, 0x01, 0x2B, 0x01, 0x00, 0x02};
    CHECK(same(r, n, e_cnt, sizeof(e_cnt)));

    /* 4. Du 5 DTC -> 23 byte (can ISO-TP nhieu frame) */
    for (int i = 0; i < DTC_COUNT; i++) {
        dtc_report(i, 1);
    }
    n = uds_handle(q_list, 3, r);
    CHECK(n == 3 + 4 * DTC_COUNT);

    /* 5. Xoa */
    const uint8_t q_clr[] = {0x14, 0xFF, 0xFF, 0xFF};
    n = uds_handle(q_clr, 4, r);
    CHECK(n == 1 && r[0] == 0x54);
    n = uds_handle(q_list, 3, r);
    CHECK(n == 3);

    /* 6. Phan hoi am */
    const uint8_t q_did[] = {0x22, 0xF1, 0x90};
    n = uds_handle(q_did, 3, r);
    const uint8_t e_11[] = {0x7F, 0x22, 0x11};
    CHECK(same(r, n, e_11, 3));
    const uint8_t q_sub[] = {0x19, 0x0A};
    n = uds_handle(q_sub, 2, r);
    const uint8_t e_12[] = {0x7F, 0x19, 0x12};
    CHECK(same(r, n, e_12, 3));
    n = uds_handle(q_list, 2, r);                /* 19 02 thieu mask */
    const uint8_t e_13[] = {0x7F, 0x19, 0x13};
    CHECK(same(r, n, e_13, 3));
    const uint8_t q_grp[] = {0x14, 0x00, 0x01, 0x00};
    n = uds_handle(q_grp, 4, r);
    const uint8_t e_31[] = {0x7F, 0x14, 0x31};
    CHECK(same(r, n, e_31, 3));

    /* 7. TesterPresent, co va khong co suppress */
    const uint8_t q_tp[] = {0x3E, 0x00};
    n = uds_handle(q_tp, 2, r);
    CHECK(n == 2 && r[0] == 0x7E);
    const uint8_t q_tps[] = {0x3E, 0x80};
    CHECK(uds_handle(q_tps, 2, r) == 0);

    printf("%s (%d loi)\n", fails == 0 ? "PASS" : "FAIL", fails);
    return fails != 0;
}
