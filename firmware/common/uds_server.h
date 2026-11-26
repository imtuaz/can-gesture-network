/*
 * uds_server.h - phan UDS (ISO 14229) cua ECU: bang DTC + xu ly request
 *
 * Chi co logic thuan (khong dung thanh ghi), nen test duoc tren PC (tests/test_uds.c).
 * ISO-TP (chia / ghep frame) nam o main.c vi can gui CAN that.
 *
 * Dich vu ho tro:
 *   0x19 01 <mask>   ReadDTCInformation - dem so DTC co (status & mask) != 0
 *   0x19 02 <mask>   ReadDTCInformation - liet ke DTC + status
 *   0x14 FF FF FF    ClearDiagnosticInformation - xoa tat ca
 *   0x3E 00          TesterPresent
 * Phan hoi duong: SID + 0x40 (0x19 -> 0x59, 0x14 -> 0x54, 0x3E -> 0x7E)
 * Phan hoi am:    7F <SID> <NRC>
 *
 * DTC 3 byte = 2 byte ma (P/C/B/U + 4 chu so hex) + 1 byte Failure Type Byte (FTB).
 *   Byte dau: 2 bit cao chon chu: 00 P, 01 C, 10 B, 11 U.
 *   Vi du U0100-87: U = 11, "0100" -> 11 00 0001 0000 0000 = 0xC100, FTB 0x87 -> 0xC10087
 *
 * Status byte (moi DTC 1 byte), chi dung 4 bit:
 *   bit0 testFailed                     - loi DANG co
 *   bit1 testFailedThisOperationCycle   - da loi trong lan bat nguon nay
 *   bit3 confirmedDTC                   - da xac nhan (ban don gian: loi 1 lan la xac nhan)
 *   bit5 testFailedSinceLastClear       - da loi ke tu lan xoa gan nhat
 *   -> DTC_AVAIL_MASK = 0x2B (cac bit ECU nay co ho tro)
 *
 * Don gian hoa so voi ECU that: DTC luu trong RAM (mat khi reset; ECU that luu flash/EEPROM),
 * khong co debounce / aging, khong co snapshot (freeze frame).
 */

#ifndef UDS_SERVER_H
#define UDS_SERVER_H

#include <stdint.h>

#define DTC_AVAIL_MASK  0x2B
#define DTC_COUNT       5

/* Thu tu trong bang */
enum {
    DTC_LOST_COMM = 0,  /* U0100-87: mat frame 0x110 (timeout)           */
    DTC_COUNTER,        /* U0100-82: alive counter sai (lap / nhay)      */
    DTC_CRC,            /* U0100-83: CRC-8 cua E2E sai                   */
    DTC_BUSOFF,         /* U0001-88: bus CAN bi bus-off                  */
    DTC_WATCHDOG        /* P0606-00: loi bo xu ly (watchdog reset)       */
};

typedef struct {
    uint32_t code;      /* 24 bit: 2 byte ma + FTB */
    uint8_t  status;
} dtc_t;

static dtc_t dtc_table[DTC_COUNT] = {
    {0xC10087, 0},
    {0xC10082, 0},
    {0xC10083, 0},
    {0xC00188, 0},
    {0x060600, 0},
};

/* Goi lien tuc voi tinh trang hien tai cua loi.
 * failed = 1: bat bit0, 1, 3, 5. failed = 0: chi tat bit0 (cac bit "lich su" giu nguyen). */
static inline void dtc_report(int i, int failed)
{
    if (failed) {
        dtc_table[i].status |= 0x01 | 0x02 | 0x08 | 0x20;
    } else {
        dtc_table[i].status &= (uint8_t)~0x01;
    }
}

static inline void dtc_clear_all(void)
{
    for (int i = 0; i < DTC_COUNT; i++) {
        dtc_table[i].status = 0;
    }
}

static inline uint32_t uds_nrc(uint8_t sid, uint8_t nrc, uint8_t *resp)
{
    resp[0] = 0x7F;
    resp[1] = sid;
    resp[2] = nrc;
    return 3;
}

/*
 * Xu ly 1 request (da ghep xong tu ISO-TP). Ghi phan hoi vao resp (can >= 3 + 4 * DTC_COUNT byte).
 * Tra ve so byte phan hoi; 0 = khong phan hoi (TesterPresent co bit "suppress").
 */
static inline uint32_t uds_handle(const uint8_t *req, uint32_t len, uint8_t *resp)
{
    if (len == 0) {
        return 0;
    }
    uint8_t sid = req[0];

    switch (sid) {
    case 0x19: {                                     /* ReadDTCInformation */
        if (len < 2) {
            return uds_nrc(sid, 0x13, resp);         /* sai do dai */
        }
        uint8_t sub = req[1];
        if (sub != 0x01 && sub != 0x02) {
            return uds_nrc(sid, 0x12, resp);         /* sub-function khong ho tro */
        }
        if (len != 3) {
            return uds_nrc(sid, 0x13, resp);
        }
        uint8_t mask = req[2];

        resp[0] = 0x59;
        resp[1] = sub;
        resp[2] = DTC_AVAIL_MASK;
        if (sub == 0x01) {                           /* chi dem */
            uint16_t count = 0;
            for (int i = 0; i < DTC_COUNT; i++) {
                if (dtc_table[i].status & mask) {
                    count++;
                }
            }
            resp[3] = 0x01;                          /* DTCFormatIdentifier: ISO 14229-1 */
            resp[4] = (uint8_t)(count >> 8);
            resp[5] = (uint8_t)count;
            return 6;
        }
        uint32_t n = 3;                              /* sub 0x02: liet ke */
        for (int i = 0; i < DTC_COUNT; i++) {
            if (dtc_table[i].status & mask) {
                resp[n++] = (uint8_t)(dtc_table[i].code >> 16);
                resp[n++] = (uint8_t)(dtc_table[i].code >> 8);
                resp[n++] = (uint8_t)dtc_table[i].code;
                resp[n++] = dtc_table[i].status & DTC_AVAIL_MASK;
            }
        }
        return n;
    }

    case 0x14: {                                     /* ClearDiagnosticInformation */
        if (len != 4) {
            return uds_nrc(sid, 0x13, resp);
        }
        uint32_t group = ((uint32_t)req[1] << 16) | ((uint32_t)req[2] << 8) | req[3];
        if (group != 0xFFFFFF) {
            return uds_nrc(sid, 0x31, resp);         /* chi ho tro "xoa tat ca" */
        }
        dtc_clear_all();
        resp[0] = 0x54;
        return 1;
    }

    case 0x3E: {                                     /* TesterPresent */
        if (len != 2) {
            return uds_nrc(sid, 0x13, resp);
        }
        if ((req[1] & 0x7F) != 0x00) {
            return uds_nrc(sid, 0x12, resp);
        }
        if (req[1] & 0x80) {
            return 0;                                /* suppressPosRspMsgIndicationBit */
        }
        resp[0] = 0x7E;
        resp[1] = 0x00;
        return 2;
    }

    default:
        return uds_nrc(sid, 0x11, resp);             /* service khong ho tro */
    }
}

#endif /* UDS_SERVER_H */
