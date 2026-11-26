#ifndef E2E_H
#define E2E_H

#include <stdint.h>

#define E2E_MAX_DELTA   3   /* chap nhan mat toi da 2 frame lien tiep (delta = 3) */

typedef enum {
    E2E_OK = 0,         /* dung, counter tang 1 */
    E2E_OK_LOST,        /* dung nhung co frame bi mat (delta 2..E2E_MAX_DELTA) */
    E2E_ERR_LEN,        /* DLC qua ngan */
    E2E_ERR_CRC,        /* CRC sai -> bo frame */
    E2E_ERR_REPEATED,   /* counter khong doi -> ben gui bi treo / gui lai du lieu cu */
    E2E_ERR_SEQUENCE    /* nhay counter qua xa -> mat nhieu frame, dong bo lai */
} e2e_result_t;

typedef struct {
    uint8_t last;       /* counter cua frame hop le gan nhat */
    uint8_t has_last;   /* 0 = chua nhan frame nao */
} e2e_rx_t;

/*
 * CRC-8 SAE J1850: da thuc 0x1D (x^8 + x^4 + x^3 + x^2 + 1), gia tri dau 0xFF, XOR cuoi 0xFF.
 * Kiem tra: crc8_j1850("123456789", 9) = 0x4B.
 * Cach tinh: dua tung byte vao, dich trai tung bit; bit 7 bi day ra = 1 thi XOR voi 0x1D
 * (giong phep chia da thuc modulo 2, chi giu lai phan du).
 */
static inline uint8_t crc8_j1850(const uint8_t *data, uint32_t len)
{
    uint8_t crc = 0xFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x1D);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return (uint8_t)(crc ^ 0xFF);
}

/* CRC cua [ID thap, ID cao, data[0..len-1]]. len <= 7 */
static inline uint8_t e2e_crc(uint16_t can_id, const uint8_t *data, uint32_t len)
{
    uint8_t buf[9];
    buf[0] = (uint8_t)(can_id & 0xFF);
    buf[1] = (uint8_t)(can_id >> 8);
    for (uint32_t i = 0; i < len; i++) {
        buf[2 + i] = data[i];
    }
    return crc8_j1850(buf, 2 + len);
}

/*
 * Ben gui: ghi counter vao data[n_payload], CRC vao data[n_payload + 1].
 * DLC cua frame = n_payload + 2. Goi xong thi tang *counter cho frame sau.
 */
static inline void e2e_protect(uint16_t can_id, uint8_t *data, uint32_t n_payload, uint8_t *counter)
{
    data[n_payload] = (uint8_t)(*counter & 0x0F);
    data[n_payload + 1] = e2e_crc(can_id, data, n_payload + 1);
    *counter = (uint8_t)((*counter + 1) & 0x0F);
}

/*
 * Ben nhan: kiem tra 1 frame. *lost = so frame bi mat (chi co nghia khi E2E_OK_LOST).
 * Chi E2E_OK va E2E_OK_LOST la du lieu dung duoc.
 *
 * Gioi han: counter 4 bit quay vong sau 16 frame. Mat dung 16 frame -> delta = 0 -> bi bao
 * nham la REPEATED. Truong hop nay do timeout lo (16 x 100 ms = 1.6 s, lon hon timeout 300 ms).
 */
static inline e2e_result_t e2e_check(e2e_rx_t *st, uint16_t can_id,
                                     const uint8_t *data, uint32_t dlc,
                                     uint32_t n_payload, uint32_t *lost)
{
    *lost = 0;
    if (dlc < n_payload + 2) {
        return E2E_ERR_LEN;
    }
    if (e2e_crc(can_id, data, n_payload + 1) != data[n_payload + 1]) {
        return E2E_ERR_CRC;
    }

    uint8_t cnt = data[n_payload] & 0x0F;
    if (!st->has_last) {                    /* frame dau tien: chua co gi de so */
        st->has_last = 1;
        st->last = cnt;
        return E2E_OK;
    }

    uint8_t delta = (uint8_t)((cnt - st->last) & 0x0F);   /* 15 -> 0 cho delta = 1 */
    if (delta == 0) {
        return E2E_ERR_REPEATED;
    }
    st->last = cnt;
    if (delta == 1) {
        return E2E_OK;
    }
    if (delta <= E2E_MAX_DELTA) {
        *lost = delta - 1;
        return E2E_OK_LOST;
    }
    return E2E_ERR_SEQUENCE;                /* da dong bo lai theo counter moi */
}

#endif /* E2E_H */
