/*
 * Moc 9 - phia STM32F103 (Blue Pill): moc 8 + chan doan UDS qua ISO-TP (doc / xoa DTC)
 *
 * MOI O MOC 9 (phan con lai giong moc 8):
 *   - STM32 la ECU (UDS server). Tester (ESP32) gui request ID 0x7E0, ECU tra loi ID 0x7E8.
 *   - ISO-TP (ISO 15765-2): chia 1 message dai thanh nhieu frame CAN 8 byte.
 *       Single Frame  (SF): byte 0 = 0x0N, N = so byte du lieu (1..7)
 *       First Frame   (FF): byte 0..1 = 0x1L LL (L = tong so byte, 12 bit), sau do 6 byte du lieu
 *       Flow Control  (FC): tester tra 0x30 BS STmin (cho phep gui tiep, BS frame / lan, cach STmin ms)
 *       Consecutive   (CF): byte 0 = 0x2N (N = so thu tu 1..15, 0, 1...), sau do 7 byte du lieu
 *     ECU nay: nhan request chi dang SF (request UDS deu ngan), gui phan hoi SF hoac FF + CF.
 *   - DTC (uds_server.h) duoc bat tu cac loi cua moc 8: timeout, E2E counter, E2E CRC, bus-off,
 *     watchdog reset.
 *   - Lenh gay loi tu ESP32 doi tu 0x7E0 sang 0x7F0 (0x7E0 nhuong cho UDS).
 *   - TXFP = 1: 3 mailbox gui theo thu tu xep hang. Mac dinh bxCAN gui theo ID, ID bang nhau
 *     thi mailbox so nho truoc -> cac CF (cung ID 0x7E8) co the bi dao thu tu.
 *
 * ---- Phan moc 8 ----
 *
 * Node 2 dong vai "ECU chap hanh": nhan goc nghieng 0x110 tu ESP32, dieu khien LED PC13
 * (LED = co cau chap hanh). Them 4 lop xu ly loi so voi moc 6:
 *
 *   1. Error confinement: doc TEC, REC, cac co EWGF / EPVF / BOFF va ma loi LEC trong CAN_ESR.
 *      ABOM = 1: phan cung tu phuc hoi sau bus-off (cho 128 lan x 11 bit lan).
 *   2. Timeout: qua 300 ms (3 chu ky) khong co 0x110 hop le -> SAFE.
 *   3. E2E: 0x110 co alive counter + CRC-8 (e2e.h). Frame sai CRC / lap lai -> bo, khong dung.
 *   4. State machine + watchdog (IWDG ~0.5 s): treo phan mem -> tu reset.
 *
 * Trang thai:
 *   SAFE     : LED tat, bo qua du lieu nghieng. Vao khi: khoi dong, timeout, bus-off.
 *              Ra khi: nhan du 3 frame hop le lien tiep (tranh bat/tat lien tuc).
 *   DEGRADED : van chay binh thuong nhung dang co canh bao: node o error passive,
 *              hoac co loi E2E trong 1 s gan nhat.
 *   NORMAL   : khong co loi.
 *
 * CAN gui ra:
 *   0x200 DLC 1: trang thai LED (khi doi)
 *   0x210 DLC 4: moi 100 ms [trang thai, TEC, REC, co loi] -> ESP32 dung de biet STM32 con song
 *     co loi: bit0 timeout, bit1 loi E2E (1 s gan nhat), bit2 error passive, bit3 bus-off,
 *             bit4 lan khoi dong nay do watchdog reset
 *
 * Lenh gay loi: go qua UART (can noi PA10), HOAC gui tu ESP32 qua CAN ID 0x7F0 byte 0 = ky tu lenh
 * (ESP32 go h / x / y -> gui 'h' / 's' / 'n'), nen khong bat buoc noi them day PA10.
 * Lenh:
 *   s = ngung gui 0x210 (gia lap STM32 im lang)   n = gui lai
 *   h = treo CPU (vong lap vo han) -> thu watchdog  r = xoa bo dem thong ke
 *
 * Phan cung giong moc 3-7:
 * - Clock: HSE 8 MHz, APB1 = 8 MHz. bxCAN remap PB8 = RX, PB9 = TX -> SN65HVD230
 * - USART1: PA9 TX, PA10 RX, 115200 -> CP2102 (PA10 khong bat buoc)
 * - PC13: LED (sang khi chan = 0)
 *
 * Project CubeIDE: chep main.c nay vao Src, e2e.h va uds_server.h vao Inc. Khong dung ngat nao.
 */

#include <stdint.h>
#include "e2e.h"
#include "uds_server.h"

#define REG(addr)   (*(volatile uint32_t *)(addr))

/* ---------- RCC ---------- */
#define RCC_BASE        0x40021000UL
#define RCC_CR          REG(RCC_BASE + 0x00)
#define RCC_CFGR        REG(RCC_BASE + 0x04)
#define RCC_APB2ENR     REG(RCC_BASE + 0x18)
#define RCC_APB1ENR     REG(RCC_BASE + 0x1C)
#define RCC_CSR         REG(RCC_BASE + 0x24)   /* co nguyen nhan reset */

/* ---------- AFIO (remap chan) ---------- */
#define AFIO_MAPR       REG(0x40010000UL + 0x04)

/* ---------- GPIO ---------- */
#define GPIOA_CRH       REG(0x40010800UL + 0x04)
#define GPIOB_CRH       REG(0x40010C00UL + 0x04)
#define GPIOB_ODR       REG(0x40010C00UL + 0x0C)
#define GPIOC_CRH       REG(0x40011000UL + 0x04)
#define GPIOC_ODR       REG(0x40011000UL + 0x0C)

/* ---------- USART1 ---------- */
#define USART1_BASE     0x40013800UL
#define USART1_SR       REG(USART1_BASE + 0x00)
#define USART1_DR       REG(USART1_BASE + 0x04)
#define USART1_BRR      REG(USART1_BASE + 0x08)
#define USART1_CR1      REG(USART1_BASE + 0x0C)

/* ---------- TIM2 (dung lam dong ho ms, khong can ngat) ---------- */
#define TIM2_CR1        REG(0x40000000UL + 0x00)
#define TIM2_EGR        REG(0x40000000UL + 0x14)
#define TIM2_CNT        REG(0x40000000UL + 0x24)
#define TIM2_PSC        REG(0x40000000UL + 0x28)
#define TIM2_ARR        REG(0x40000000UL + 0x2C)

/* ---------- IWDG (watchdog doc lap, chay bang LSI ~40 kHz) ---------- */
#define IWDG_KR         REG(0x40003000UL + 0x00)
#define IWDG_PR         REG(0x40003000UL + 0x04)
#define IWDG_RLR        REG(0x40003000UL + 0x08)
#define IWDG_SR         REG(0x40003000UL + 0x0C)
#define DBGMCU_CR       REG(0xE0042004UL)

/* ---------- bxCAN ---------- */
#define CAN_BASE        0x40006400UL
#define CAN_MCR         REG(CAN_BASE + 0x000)
#define CAN_MSR         REG(CAN_BASE + 0x004)
#define CAN_TSR         REG(CAN_BASE + 0x008)
#define CAN_RF0R        REG(CAN_BASE + 0x00C)
#define CAN_ESR         REG(CAN_BASE + 0x018)
#define CAN_BTR         REG(CAN_BASE + 0x01C)
#define CAN_TIR(k)      REG(CAN_BASE + 0x180 + 0x10 * (k))   /* mailbox gui k = 0, 1, 2 */
#define CAN_TDTR(k)     REG(CAN_BASE + 0x184 + 0x10 * (k))
#define CAN_TDLR(k)     REG(CAN_BASE + 0x188 + 0x10 * (k))
#define CAN_TDHR(k)     REG(CAN_BASE + 0x18C + 0x10 * (k))
#define CAN_RI0R        REG(CAN_BASE + 0x1B0)
#define CAN_RDT0R       REG(CAN_BASE + 0x1B4)
#define CAN_RDL0R       REG(CAN_BASE + 0x1B8)
#define CAN_RDH0R       REG(CAN_BASE + 0x1BC)
#define CAN_FMR         REG(CAN_BASE + 0x200)
#define CAN_FM1R        REG(CAN_BASE + 0x204)
#define CAN_FS1R        REG(CAN_BASE + 0x20C)
#define CAN_FFA1R       REG(CAN_BASE + 0x214)
#define CAN_FA1R        REG(CAN_BASE + 0x21C)
#define CAN_F0R1        REG(CAN_BASE + 0x240)
#define CAN_F0R2        REG(CAN_BASE + 0x244)

/* Cac bit trong CAN_ESR */
#define ESR_EWGF        (1UL << 0)      /* TEC hoac REC >= 96: canh bao */
#define ESR_EPVF        (1UL << 1)      /* TEC hoac REC >= 128: error passive */
#define ESR_BOFF        (1UL << 2)      /* TEC > 255: bus-off */
#define ESR_TEC(esr)    (((esr) >> 16) & 0xFF)
#define ESR_REC(esr)    (((esr) >> 24) & 0xFF)
#define ESR_LEC(esr)    (((esr) >> 4) & 0x7)

/* ================= Thong so ================= */
#define ID_TILT         0x110   /* ESP32 -> STM32: roll, pitch, counter, CRC (DLC 6) */
#define ID_LED_STATUS   0x200   /* STM32 -> ESP32: trang thai LED */
#define ID_NODE2_STATUS 0x210   /* STM32 -> ESP32: trang thai node, moi 100 ms */
#define ID_TEST_CMD     0x7F0   /* ESP32 -> STM32: lenh gay loi (1 byte), thay cho go UART */
#define ID_UDS_REQ      0x7E0   /* tester -> ECU: request UDS (ISO-TP) va Flow Control */
#define ID_UDS_RESP     0x7E8   /* ECU -> tester: phan hoi UDS (ISO-TP) */

#define TILT_ON         3000    /* 30.00 do -> bat LED */
#define TILT_OFF        2500    /* 25.00 do -> tat LED */

#define RX_TIMEOUT_MS   300     /* 3 chu ky 0x110 */
#define E2E_HOLD_MS     1000    /* loi E2E giu trang thai DEGRADED them 1 s */
#define VALID_TO_EXIT   3       /* so frame hop le lien tiep de ra khoi SAFE */
#define STATUS_MS       100     /* chu ky gui 0x210 */
#define PRINT_MS        1000    /* chu ky in thong ke */

#define ST_NORMAL       0
#define ST_DEGRADED     1
#define ST_SAFE         2

#define F_TIMEOUT       (1u << 0)
#define F_E2E           (1u << 1)
#define F_PASSIVE       (1u << 2)
#define F_BUSOFF        (1u << 3)
#define F_WDG_RESET     (1u << 4)

/* ================= Dong ho 1 ms ================= */
/*
 * TIM2 dem len 1 lan moi 1 ms (8 MHz / 8000). Khong dung ngat nen khong phu thuoc
 * bang vector / file stm32f1xx_it.c cua project.
 * TIM2 chi 16 bit (quay vong sau 65.5 s), nen ms_update() cong don phan chenh lech
 * vao g_ms 32 bit. Chi can goi ms_update() it nhat 1 lan moi 65 s (vong lap goi lien tuc).
 */
static uint32_t g_ms = 0;
static uint16_t last_cnt = 0;

static void timebase_init(void)
{
    RCC_APB1ENR |= (1UL << 0);  /* bat clock TIM2 */
    TIM2_PSC = 8000 - 1;        /* 8 MHz / 8000 = 1 kHz */
    TIM2_ARR = 0xFFFF;
    TIM2_EGR = 1;               /* UG: nap PSC ngay */
    TIM2_CNT = 0;
    TIM2_CR1 = 1;               /* CEN: bat dau dem */
}

static void ms_update(void)
{
    uint16_t cnt = (uint16_t)TIM2_CNT;
    g_ms += (uint16_t)(cnt - last_cnt);    /* phep tru 16 bit tu xu ly quay vong */
    last_cnt = cnt;
}

/* ================= Clock HSE (giong moc 3) ================= */
void SystemInit(void)
{
    uint32_t timeout = 100000;

    RCC_CR |= (1UL << 16);                       /* HSEON */
    while ((RCC_CR & (1UL << 17)) == 0) {        /* cho HSERDY */
        if (--timeout == 0) {
            return;                              /* khong co thach anh: giu HSI */
        }
    }
    RCC_CFGR = (RCC_CFGR & ~3UL) | 1UL;          /* chon HSE */
    while (((RCC_CFGR >> 2) & 3UL) != 1UL) {
    }
}

/* ================= Watchdog ================= */
/*
 * LSI ~40 kHz (thuc te 30..60 kHz), chia 32 -> ~1.25 kHz; dem 625 -> ~0.5 s
 * (0.33..0.67 s tuy LSI). Phai "cho an" (ghi 0xAAAA) truoc khi dem ve 0, neu khong chip reset.
 */
static void iwdg_init(void)
{
    DBGMCU_CR |= (1UL << 8);    /* DBG_IWDG_STOP: dung watchdog khi dang dung o breakpoint */
    /* Phai START truoc: ghi 0xCCCC moi bat dao dong LSI. Neu ghi PR/RLR truoc khi LSI chay,
     * co PVU/RVU trong IWDG_SR khong bao gio ve 0 -> vong cho ben duoi treo mai */
    IWDG_KR  = 0xCCCC;          /* bat dau chay (va bat LSI), tu day khong tat duoc nua */
    IWDG_KR  = 0x5555;          /* mo khoa PR, RLR */
    IWDG_PR  = 3;               /* chia 32 */
    IWDG_RLR = 625;
    while (IWDG_SR != 0) {      /* cho gia tri moi duoc nap (vai chu ky LSI) */
    }
    IWDG_KR  = 0xAAAA;          /* nap lai bo dem voi gia tri moi */
}

static void iwdg_feed(void)
{
    IWDG_KR = 0xAAAA;
}

/* Tra ve 1 neu lan reset vua roi do watchdog. Xoa co de lan sau doc dung */
static int reset_by_watchdog(void)
{
    int wdg = (RCC_CSR & (1UL << 29)) != 0;      /* IWDGRSTF */
    RCC_CSR |= (1UL << 24);                      /* RMVF: xoa tat ca co reset */
    return wdg;
}

/* ================= UART ================= */
static void uart1_init(void)
{
    RCC_APB2ENR |= (1UL << 2) | (1UL << 14);   /* GPIOA, USART1 */
    GPIOA_CRH &= ~(0xFUL << 4);
    GPIOA_CRH |=  (0xBUL << 4);                /* PA9: AF push-pull */
    GPIOA_CRH &= ~(0xFUL << 8);
    GPIOA_CRH |=  (0x4UL << 8);                /* PA10: input floating */
    USART1_BRR = 0x45;                         /* 115200 @ 8 MHz */
    USART1_CR1 = (1UL << 3) | (1UL << 2) | (1UL << 13);   /* TE, RE, UE */
}

static void uart1_send_char(char c)
{
    while ((USART1_SR & (1UL << 7)) == 0) {
    }
    USART1_DR = (uint32_t)c;
}

static void print_str(const char *s)
{
    while (*s != '\0') {
        uart1_send_char(*s);
        s++;
    }
}

static void print_dec(uint32_t value)
{
    char buf[10];
    int n = 0;
    do {
        buf[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value > 0);
    while (n > 0) {
        uart1_send_char(buf[--n]);
    }
}

/* Tra ve ky tu vua nhan, hoac 0 neu chua co (khong cho) */
static char uart1_poll_char(void)
{
    if (USART1_SR & (1UL << 5)) {               /* RXNE */
        return (char)(USART1_DR & 0xFF);
    }
    return 0;
}

/* In "[t=12345 ms] " o dau dong log */
static void print_time(void)
{
    ms_update();
    print_str("[t=");
    print_dec(g_ms);
    print_str(" ms] ");
}

/* ================= LED ================= */
static void led_init(void)
{
    RCC_APB2ENR |= (1UL << 4);
    GPIOC_CRH &= ~(0xFUL << 20);
    GPIOC_CRH |=  (0x2UL << 20);               /* PC13: output */
}

static void led_set(uint32_t on)
{
    if (on) {
        GPIOC_ODR &= ~(1UL << 13);
    } else {
        GPIOC_ODR |=  (1UL << 13);
    }
}

/* ================= bxCAN (giong moc 7) ================= */
static int can_init(void)
{
    uint32_t timeout;

    RCC_APB2ENR |= (1UL << 3) | (1UL << 0);    /* GPIOB, AFIO */
    RCC_APB1ENR |= (1UL << 25);                /* CAN */

    AFIO_MAPR &= ~(3UL << 13);
    AFIO_MAPR |=  (2UL << 13);                 /* remap PB8/PB9 */

    GPIOB_CRH &= ~(0xFFUL << 0);
    GPIOB_CRH |=  (0x8UL << 0) | (0xBUL << 4); /* PB8 input pull-up, PB9 AF PP */
    GPIOB_ODR |=  (1UL << 8);

    CAN_MCR &= ~(1UL << 1);                    /* SLEEP = 0 */
    CAN_MCR |=  (1UL << 0);                    /* INRQ = 1 */
    timeout = 1000000;
    while ((CAN_MSR & (1UL << 0)) == 0) {
        if (--timeout == 0) {
            return 0;
        }
    }

    /* ABOM = 1: tu phuc hoi bus-off. Khong bat NART -> frame khong duoc ACK se gui lai mai */
    CAN_MCR |= (1UL << 6);

    /* TXFP = 1: gui theo thu tu yeu cau (FIFO), de cac CF cua ISO-TP khong bi dao thu tu */
    CAN_MCR |= (1UL << 2);

    /* 500 kbit/s: SJW 2, BS2 2, BS1 13, BRP 1 */
    CAN_BTR = ((2UL - 1) << 24) | ((2UL - 1) << 20) | ((13UL - 1) << 16) | (1UL - 1);

    /* Bo loc 0: nhan tat ca -> FIFO 0 */
    CAN_FMR  |=  (1UL << 0);
    CAN_FA1R &= ~(1UL << 0);
    CAN_FS1R |=  (1UL << 0);
    CAN_FM1R &= ~(1UL << 0);
    CAN_F0R1 = 0;
    CAN_F0R2 = 0;
    CAN_FFA1R &= ~(1UL << 0);
    CAN_FA1R |=  (1UL << 0);
    CAN_FMR  &= ~(1UL << 0);

    CAN_MCR &= ~(1UL << 0);                    /* roi init, cho 11 bit lan */
    timeout = 1000000;
    while ((CAN_MSR & (1UL << 0)) != 0) {
        if (--timeout == 0) {
            return 0;
        }
    }
    return 1;
}

static void can_read_frame(uint32_t *id, uint32_t *dlc, uint8_t data[8])
{
    uint32_t ri = CAN_RI0R;
    uint32_t lo = CAN_RDL0R;
    uint32_t hi = CAN_RDH0R;

    if (ri & (1UL << 2)) {
        *id = ri >> 3;                           /* ID mo rong 29 bit */
    } else {
        *id = ri >> 21;                          /* ID chuan 11 bit */
    }
    *dlc = CAN_RDT0R & 0xF;
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t word = (i < 4) ? lo : hi;
        data[i] = (uint8_t)(word >> (8 * (i % 4)));
    }
    CAN_RF0R |= (1UL << 5);                      /* RFOM0: tra frame */
}

/* Gui frame ID chuan qua mailbox trong bat ky (0, 1, 2).
 * Tra ve 0 neu ca 3 mailbox dang ban (vi du: khong ai ACK nen frame cu gui lai mai). */
static int can_send(uint32_t id, const uint8_t *data, uint32_t len)
{
    uint32_t lo = 0;
    uint32_t hi = 0;
    int k;

    for (k = 0; k < 3; k++) {
        if (CAN_TSR & (1UL << (26 + k))) {       /* TMEk = 1: mailbox k trong */
            break;
        }
    }
    if (k == 3) {
        return 0;
    }

    for (uint32_t i = 0; i < len; i++) {
        if (i < 4) {
            lo |= (uint32_t)data[i] << (8 * i);
        } else {
            hi |= (uint32_t)data[i] << (8 * (i - 4));
        }
    }
    CAN_TIR(k)  = id << 21;
    CAN_TDTR(k) = len;
    CAN_TDLR(k) = lo;
    CAN_TDHR(k) = hi;
    CAN_TIR(k) |= (1UL << 0);                    /* TXRQ */
    return 1;
}

/* Huy moi frame dang cho trong 3 mailbox (ABRQ0/1/2). Dung khi frame cu khong con dung nua */
static void can_abort_all(void)
{
    CAN_TSR = (1UL << 7) | (1UL << 15) | (1UL << 23);
}

/* ================= Ten de in ================= */
static const char *state_name(uint8_t st)
{
    switch (st) {
    case ST_NORMAL:   return "NORMAL";
    case ST_DEGRADED: return "DEGRADED";
    default:          return "SAFE";
    }
}

/* LEC = Last Error Code: loai loi bxCAN thay gan nhat tren bus */
static const char *lec_name(uint32_t lec)
{
    switch (lec) {
    case 1:  return "stuff";
    case 2:  return "form";
    case 3:  return "ACK";
    case 4:  return "bit recessive";    /* muon gui 1 (lan) nhung doc lai 0 (troi) */
    case 5:  return "bit dominant";     /* muon gui 0 (troi) nhung doc lai 1 (lan) */
    case 6:  return "CRC";
    default: return "-";
    }
}

static const char *e2e_name(e2e_result_t r)
{
    switch (r) {
    case E2E_OK:           return "OK";
    case E2E_OK_LOST:      return "mat frame";
    case E2E_ERR_LEN:      return "sai DLC";
    case E2E_ERR_CRC:      return "sai CRC";
    case E2E_ERR_REPEATED: return "counter lap lai";
    default:               return "nhay counter";
    }
}

static int16_t get_int16_le(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* ================= Bien trang thai ================= */
static uint8_t  state = ST_SAFE;        /* khoi dong o SAFE: chua co du lieu thi chua chap hanh */
static uint8_t  led_state = 0;
static uint8_t  wdg_reset = 0;
static uint8_t  status_tx_on = 1;

static uint32_t last_valid_ms = 0;      /* lan cuoi nhan 0x110 hop le */
static uint32_t last_e2e_err_ms = 0;
static uint8_t  had_e2e_err = 0;
static uint32_t valid_streak = 0;       /* so frame hop le lien tiep */

/* Thong ke (in moi 1 s, xoa bang lenh 'r') */
static uint32_t n_ok, n_lost, n_crc, n_rep, n_seq, n_len;
static uint32_t n_lec, n_busoff, n_tx_busy;
static uint32_t last_lec = 0;

static void stats_clear(void)
{
    n_ok = n_lost = n_crc = n_rep = n_seq = n_len = 0;
    n_lec = n_busoff = n_tx_busy = 0;
    last_lec = 0;
}

static void set_led(uint8_t on)
{
    if (on == led_state) {
        return;
    }
    led_state = on;
    led_set(on);
    if (!can_send(ID_LED_STATUS, &led_state, 1)) {
        n_tx_busy++;
        can_abort_all();
    }
}

/* Thoi diem loi E2E gan nhat, tach theo loai (de bat dung DTC) */
static uint32_t last_crc_err_ms = 0, last_cnt_err_ms = 0;
static uint8_t  had_crc_err = 0, had_cnt_err = 0;

/* ================= Xu ly 1 frame 0x110 ================= */
static e2e_rx_t e2e_tilt = {0, 0};

static void handle_tilt(const uint8_t *data, uint32_t dlc)
{
    uint32_t lost;
    e2e_result_t r = e2e_check(&e2e_tilt, ID_TILT, data, dlc, 4, &lost);

    if (r == E2E_OK || r == E2E_OK_LOST) {
        n_ok++;
        n_lost += lost;
        last_valid_ms = g_ms;
        valid_streak++;
        if (r == E2E_OK_LOST) {
            print_time();
            print_str("E2E: mat ");
            print_dec(lost);
            print_str(" frame (van dung frame nay)\r\n");
        }

        /* Chi chap hanh khi khong o SAFE */
        if (state != ST_SAFE) {
            int16_t roll = get_int16_le(&data[0]);
            int32_t abs_roll = (roll < 0) ? -(int32_t)roll : roll;
            if (led_state == 0 && abs_roll > TILT_ON) {
                set_led(1);
            } else if (led_state == 1 && abs_roll < TILT_OFF) {
                set_led(0);
            }
        }
        return;
    }

    /* Frame khong dung duoc: bo, khong cap nhat last_valid_ms */
    switch (r) {
    case E2E_ERR_CRC:      n_crc++; last_crc_err_ms = g_ms; had_crc_err = 1; break;
    case E2E_ERR_REPEATED: n_rep++; last_cnt_err_ms = g_ms; had_cnt_err = 1; break;
    case E2E_ERR_SEQUENCE: n_seq++; last_cnt_err_ms = g_ms; had_cnt_err = 1; break;
    default:               n_len++; break;
    }
    had_e2e_err = 1;
    last_e2e_err_ms = g_ms;
    valid_streak = 0;

    /* In loi dau tien moi 1 s cho khoi tran UART */
    static uint32_t last_print = 0;
    if (g_ms - last_print >= 1000) {
        last_print = g_ms;
        print_time();
        print_str("E2E loi: ");
        print_str(e2e_name(r));
        print_str(" -> bo frame\r\n");
    }
}

/* ================= Theo doi bo dem loi CAN ================= */
static uint32_t prev_esr_flags = 0;
static uint32_t busoff_start_ms = 0;

static void monitor_can_errors(void)
{
    uint32_t esr = CAN_ESR;
    uint32_t lec = ESR_LEC(esr);
    uint32_t flags = esr & (ESR_EWGF | ESR_EPVF | ESR_BOFF);

    /* LEC giu gia tri den khi co loi moi. Ghi 7 vao LEC (gia tri phan cung khong bao gio
     * dat) de lan doc sau biet do la loi moi hay cu */
    if (lec != 0 && lec != 7) {
        n_lec++;
        last_lec = lec;
        CAN_ESR = (7UL << 4);
    }

    if (flags == prev_esr_flags) {
        return;
    }

    /* Khi chap CANH-CANL, node bus-off -> tu phuc hoi -> bus-off lai vai ms 1 lan.
     * Gioi han in 10 dong/s (van dem du trong n_busoff) de UART khong lam cham vong lap */
    static uint32_t last_print = 0;
    if ((flags & ESR_BOFF) && !(prev_esr_flags & ESR_BOFF)) {
        n_busoff++;
        busoff_start_ms = g_ms;
        dtc_report(DTC_BUSOFF, 1);       /* bus-off co the chi dai ~3 ms: bat DTC ngay luc thay */
        /* Huy cac frame dang cho trong 3 mailbox (ABRQ0/1/2). Neu khong, sau khi phuc hoi
         * bxCAN gui lai frame CU (vi du 0x210 bao NORMAL tu truoc luc bus-off): du lieu
         * da het han. Da thay hien tuong nay khi test chap CANH-CANL. */
        can_abort_all();
    }
    if (g_ms - last_print < 100) {
        prev_esr_flags = flags;
        return;
    }
    last_print = g_ms;

    print_time();
    if ((flags & ESR_BOFF) && !(prev_esr_flags & ESR_BOFF)) {
        print_str("BUS-OFF (TEC > 255)");
    } else if (!(flags & ESR_BOFF) && (prev_esr_flags & ESR_BOFF)) {
        print_str("Het bus-off sau ");
        print_dec(g_ms - busoff_start_ms);
        print_str(" ms (ABOM tu phuc hoi)");
    } else if (flags & ESR_EPVF) {
        print_str("ERROR PASSIVE");
    } else if (flags & ESR_EWGF) {
        print_str("ERROR WARNING (>= 96)");
    } else {
        print_str("ERROR ACTIVE (binh thuong)");
    }
    print_str(" | TEC=");
    print_dec(ESR_TEC(esr));
    print_str(" REC=");
    print_dec(ESR_REC(esr));
    print_str(" loi gan nhat: ");
    print_str(lec_name(last_lec));
    print_str("\r\n");

    prev_esr_flags = flags;
}

/* ================= State machine ================= */
/* Tinh co loi hien tai (cung la byte 3 cua frame 0x210) */
static uint8_t current_faults(void)
{
    uint32_t esr = CAN_ESR;
    uint8_t f = 0;

    if (g_ms - last_valid_ms > RX_TIMEOUT_MS) {
        f |= F_TIMEOUT;
    }
    if (had_e2e_err && (g_ms - last_e2e_err_ms < E2E_HOLD_MS)) {
        f |= F_E2E;
    }
    if (esr & ESR_EPVF) {
        f |= F_PASSIVE;
    }
    if (esr & ESR_BOFF) {
        f |= F_BUSOFF;
    }
    if (wdg_reset) {
        f |= F_WDG_RESET;
    }
    return f;
}

static void update_state(void)
{
    uint8_t f = current_faults();
    uint8_t next;

    if (f & (F_TIMEOUT | F_BUSOFF)) {
        next = ST_SAFE;
        valid_streak = 0;
    } else if (state == ST_SAFE && valid_streak < VALID_TO_EXIT) {
        next = ST_SAFE;                          /* chua du frame hop le lien tiep */
    } else if (f & (F_E2E | F_PASSIVE)) {
        next = ST_DEGRADED;
    } else {
        next = ST_NORMAL;
    }

    if (next == state) {
        return;
    }

    print_time();
    print_str(state_name(state));
    print_str(" -> ");
    print_str(state_name(next));
    print_str(" (");
    if (f & F_BUSOFF)       print_str("bus-off ");
    if (f & F_TIMEOUT)      print_str("timeout 0x110 ");
    if (f & F_E2E)          print_str("loi E2E ");
    if (f & F_PASSIVE)      print_str("error passive ");
    if (f == 0 || f == F_WDG_RESET) print_str("het loi ");
    print_str(")\r\n");

    state = next;
    if (state == ST_SAFE) {
        set_led(0);                              /* trang thai an toan: tat co cau chap hanh */
    }
}

static void send_status(void)
{
    uint32_t esr = CAN_ESR;
    uint8_t d[4];

    d[0] = state;
    d[1] = (uint8_t)ESR_TEC(esr);
    d[2] = (uint8_t)ESR_REC(esr);
    d[3] = current_faults();
    if (!can_send(ID_NODE2_STATUS, d, 4)) {
        /* Ca 3 mailbox dang ket (khong ai ACK -> bxCAN gui lai mai). Cac frame trong do la
         * trang thai cu: huy di de khi bus binh thuong lai, frame dau tien gui ra la trang thai
         * MOI NHAT (lan test T6 da thay ESP32 nhan 3 frame "NORMAL" cu sau khi cam lai day) */
        n_tx_busy++;
        can_abort_all();
    }
}

static void print_stats(void)
{
    uint32_t esr = CAN_ESR;

    print_time();
    print_str(state_name(state));
    print_str(" TEC=");
    print_dec(ESR_TEC(esr));
    print_str(" REC=");
    print_dec(ESR_REC(esr));
    print_str(" | ok=");
    print_dec(n_ok);
    print_str(" mat=");
    print_dec(n_lost);
    print_str(" crc=");
    print_dec(n_crc);
    print_str(" lap=");
    print_dec(n_rep);
    print_str(" nhay=");
    print_dec(n_seq);
    print_str(" dlc=");
    print_dec(n_len);
    print_str(" | loi bus=");
    print_dec(n_lec);
    print_str(" (");
    print_str(lec_name(last_lec));
    print_str(") busoff=");
    print_dec(n_busoff);
    print_str(" tx ban=");
    print_dec(n_tx_busy);
    print_str("\r\n");
}

static void handle_uart_command(char c)
{
    switch (c) {
    case 's':
        status_tx_on = 0;
        print_str(">> Ngung gui 0x210\r\n");
        break;
    case 'n':
        status_tx_on = 1;
        print_str(">> Gui lai 0x210\r\n");
        break;
    case 'r':
        stats_clear();
        print_str(">> Xoa thong ke\r\n");
        break;
    case 'h':
        print_str(">> Treo CPU, cho watchdog reset...\r\n");
        while (1) {                              /* khong cho an watchdog nua */
        }
    default:
        break;
    }
}

/* ================= DTC: cap nhat tu cac loi cua moc 8 ================= */
static void dtc_update(void)
{
    uint8_t f = current_faults();

    dtc_report(DTC_LOST_COMM, (f & F_TIMEOUT) != 0);
    dtc_report(DTC_COUNTER, had_cnt_err && (g_ms - last_cnt_err_ms < E2E_HOLD_MS));
    dtc_report(DTC_CRC, had_crc_err && (g_ms - last_crc_err_ms < E2E_HOLD_MS));
    dtc_report(DTC_BUSOFF, (f & F_BUSOFF) != 0);
}

/* ================= ISO-TP: gui phan hoi ================= */
#define TP_IDLE     0
#define TP_SEND_SF  1       /* cho mailbox trong de gui Single Frame */
#define TP_SEND_FF  2       /* cho mailbox trong de gui First Frame */
#define TP_WAIT_FC  3       /* da gui FF / het 1 block, cho Flow Control tu tester */
#define TP_SEND_CF  4       /* dang gui Consecutive Frame */
#define TP_TIMEOUT_MS 1000  /* N_Bs: cho FC toi da 1 s */

static uint8_t  tp_buf[3 + 4 * DTC_COUNT];
static uint32_t tp_len, tp_pos;
static uint8_t  tp_state = TP_IDLE;
static uint8_t  tp_sn;              /* so thu tu CF tiep theo (4 bit) */
static uint8_t  tp_bs, tp_bs_cnt;   /* block size tester cho phep, so CF da gui trong block */
static uint32_t tp_stmin;           /* ms giua 2 CF */
static uint32_t tp_next_ms, tp_deadline;

/* Gui 1 frame 8 byte len 0x7E8, phan thua dien 0xAA (padding) */
static int tp_send_frame(const uint8_t *b, uint32_t n)
{
    uint8_t f[8];
    for (uint32_t i = 0; i < 8; i++) {
        f[i] = (i < n) ? b[i] : 0xAA;
    }
    return can_send(ID_UDS_RESP, f, 8);
}

/* Bat dau gui 1 phan hoi UDS */
static void tp_start(const uint8_t *msg, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        tp_buf[i] = msg[i];
    }
    tp_len = len;
    tp_state = (len <= 7) ? TP_SEND_SF : TP_SEND_FF;
}

/* Tester gui Flow Control: 0x3S BS STmin (S: 0 = gui tiep, 1 = cho, 2 = tran bo dem) */
static void tp_on_fc(const uint8_t *d)
{
    if (tp_state != TP_WAIT_FC) {
        return;
    }
    uint8_t fs = d[0] & 0x0F;
    if (fs == 0) {
        tp_bs = d[1];
        tp_bs_cnt = 0;
        /* STmin 0x00..0x7F = ms; 0xF1..0xF9 = 100..900 us -> lam tron 1 ms; con lai: 127 ms */
        tp_stmin = (d[2] <= 0x7F) ? d[2] : ((d[2] >= 0xF1 && d[2] <= 0xF9) ? 1 : 127);
        tp_next_ms = g_ms;
        tp_state = TP_SEND_CF;
    } else if (fs == 1) {
        tp_deadline = g_ms + TP_TIMEOUT_MS;      /* WAIT: tester xin cho them */
    } else {
        print_str("ISO-TP: tester bao tran bo dem, huy\r\n");
        tp_state = TP_IDLE;
    }
}

/* Goi trong vong lap chinh */
static void tp_poll(void)
{
    uint8_t f[8];

    switch (tp_state) {
    case TP_SEND_SF:
        f[0] = (uint8_t)tp_len;                  /* PCI 0x0N */
        for (uint32_t i = 0; i < tp_len; i++) {
            f[1 + i] = tp_buf[i];
        }
        if (tp_send_frame(f, 1 + tp_len)) {
            tp_state = TP_IDLE;
        }
        break;

    case TP_SEND_FF:
        f[0] = (uint8_t)(0x10 | (tp_len >> 8));  /* PCI 0x1L LL */
        f[1] = (uint8_t)tp_len;
        for (uint32_t i = 0; i < 6; i++) {
            f[2 + i] = tp_buf[i];
        }
        if (tp_send_frame(f, 8)) {
            tp_pos = 6;
            tp_sn = 1;
            tp_deadline = g_ms + TP_TIMEOUT_MS;
            tp_state = TP_WAIT_FC;
        }
        break;

    case TP_WAIT_FC:
        if ((int32_t)(g_ms - tp_deadline) >= 0) {
            print_str("ISO-TP: khong nhan duoc Flow Control, huy\r\n");
            tp_state = TP_IDLE;
        }
        break;

    case TP_SEND_CF: {
        if ((int32_t)(g_ms - tp_next_ms) < 0) {
            break;                               /* chua het STmin */
        }
        uint32_t n = tp_len - tp_pos;
        if (n > 7) {
            n = 7;
        }
        f[0] = (uint8_t)(0x20 | tp_sn);          /* PCI 0x2N */
        for (uint32_t i = 0; i < n; i++) {
            f[1 + i] = tp_buf[tp_pos + i];
        }
        if (!tp_send_frame(f, 1 + n)) {
            break;                               /* mailbox ban, lan sau thu lai */
        }
        tp_pos += n;
        tp_sn = (tp_sn + 1) & 0x0F;
        tp_next_ms = g_ms + tp_stmin;
        if (tp_pos >= tp_len) {
            tp_state = TP_IDLE;                  /* xong */
        } else if (tp_bs != 0 && ++tp_bs_cnt >= tp_bs) {
            tp_bs_cnt = 0;                       /* het 1 block: cho FC tiep */
            tp_deadline = g_ms + TP_TIMEOUT_MS;
            tp_state = TP_WAIT_FC;
        }
        break;
    }

    default:
        break;
    }
}

/* Frame tren 0x7E0: request (SF) hoac Flow Control */
static void handle_uds_frame(const uint8_t *d, uint32_t dlc)
{
    uint8_t pci = d[0] >> 4;

    if (pci == 3 && dlc >= 3) {
        tp_on_fc(d);
        return;
    }
    if (pci != 0) {
        return;                                  /* request nhieu frame: khong ho tro */
    }
    uint32_t n = d[0] & 0x0F;
    if (n == 0 || n > 7 || dlc < n + 1 || tp_state != TP_IDLE) {
        return;                                  /* sai do dai, hoac dang ban gui phan hoi truoc */
    }

    uint8_t resp[3 + 4 * DTC_COUNT];
    uint32_t rlen = uds_handle(&d[1], n, resp);

    print_time();
    print_str("UDS request SID 0x");
    uart1_send_char("0123456789ABCDEF"[d[1] >> 4]);
    uart1_send_char("0123456789ABCDEF"[d[1] & 0xF]);
    print_str(" -> ");
    print_dec(rlen);
    print_str(" byte phan hoi\r\n");

    if (rlen > 0) {
        tp_start(resp, rlen);
    }
}

int main(void)
{
    uint32_t id;
    uint32_t dlc;
    uint8_t  data[8];
    uint32_t next_status = 0;
    uint32_t next_print = 0;

    wdg_reset = (uint8_t)reset_by_watchdog();

    led_init();
    led_set(0);
    uart1_init();
    timebase_init();
    print_str("\r\nBlue Pill - moc 9: xu ly loi CAN + UDS doc DTC (0x7E0 / 0x7E8)\r\n");
    if (wdg_reset) {
        dtc_report(DTC_WATCHDOG, 1);             /* su kien: ghi lai roi tat bit "dang loi" */
        dtc_report(DTC_WATCHDOG, 0);
        print_str("!!! Lan khoi dong nay do WATCHDOG reset (phan mem bi treo truoc do)\r\n");
    }

    if (!can_init()) {
        print_str("Loi: CAN init bi treo (kiem tra SN65HVD230, PB8/PB9)\r\n");
        while (1) {
            GPIOC_ODR ^= (1UL << 13);            /* LED nhay nhanh = init loi */
            for (volatile uint32_t i = 0; i < 100000; i++) {
            }
        }
    }
    print_str("CAN init OK. Trang thai dau: SAFE, cho 3 frame 0x110 hop le\r\n");
    print_str("Lenh: s/n = tat/bat 0x210, h = treo CPU, r = xoa thong ke\r\n");

    iwdg_init();
    ms_update();
    last_valid_ms = g_ms;

    while (1) {
        iwdg_feed();
        ms_update();

        /* 1. Doc het frame trong FIFO 0 */
        while (CAN_RF0R & 0x3) {
            can_read_frame(&id, &dlc, data);
            if (id == ID_TILT) {
                handle_tilt(data, dlc);
            } else if (id == ID_UDS_REQ && dlc >= 1) {
                handle_uds_frame(data, dlc);
            } else if (id == ID_TEST_CMD && dlc >= 1) {
                print_str(">> Lenh qua CAN: ");
                uart1_send_char((char)data[0]);
                print_str("\r\n");
                handle_uart_command((char)data[0]);
            }
        }

        /* 2. Bo dem loi, trang thai */
        monitor_can_errors();
        update_state();
        dtc_update();
        tp_poll();

        /* 3. Gui trang thai moi 100 ms */
        if ((int32_t)(g_ms - next_status) >= 0) {
            next_status += STATUS_MS;
            if (status_tx_on) {
                send_status();
            }
        }

        /* 4. In thong ke moi 1 s (UART blocking ~9 ms, FIFO 3 o van du vi 0x110 cach 100 ms) */
        if ((int32_t)(g_ms - next_print) >= 0) {
            next_print += PRINT_MS;
            print_stats();
        }

        /* 5. Lenh tu terminal */
        char c = uart1_poll_char();
        if (c != 0) {
            handle_uart_command(c);
        }
    }
}
