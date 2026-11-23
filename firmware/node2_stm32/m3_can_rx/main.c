/*
 * Moc 3 - phia STM32F103 (Blue Pill): nhan frame CAN tu ESP32, in qua UART
 *
 * - Clock: HSE 8 MHz (thach anh, chuyen trong SystemInit), APB1 = 8 MHz
 * - bxCAN remap: PB8 = CAN_RX, PB9 = CAN_TX  -> noi voi SN65HVD230 #1
 * - USART1: PA9 TX, PA10 RX, 115200 (giong moc 1) -> CP2102
 * - LED PC13 dao trang thai moi khi nhan duoc 1 frame
 *
 * Bit timing 500 kbit/s:
 *   f_APB1 = 8 MHz, BRP = 1  -> 1 tq = 125 ns
 *   1 bit = 1 (sync) + 13 (BS1) + 2 (BS2) = 16 tq = 2 us -> 500 kbit/s
 *   diem lay mau = (1 + 13) / 16 = 87.5 %
 */

#include <stdint.h>

#define REG(addr)   (*(volatile uint32_t *)(addr))

/* ---------- RCC ---------- */
#define RCC_BASE        0x40021000UL
#define RCC_APB2ENR     REG(RCC_BASE + 0x18)
#define RCC_APB1ENR     REG(RCC_BASE + 0x1C)

/* ---------- AFIO (remap chan) ---------- */
#define AFIO_BASE       0x40010000UL
#define AFIO_MAPR       REG(AFIO_BASE + 0x04)

/* ---------- GPIO ---------- */
#define GPIOA_CRH       REG(0x40010800UL + 0x04)
#define GPIOB_CRH       REG(0x40010C00UL + 0x04)
#define GPIOB_IDR       REG(0x40010C00UL + 0x08)
#define GPIOB_ODR       REG(0x40010C00UL + 0x0C)
#define GPIOC_CRH       REG(0x40011000UL + 0x04)
#define GPIOC_ODR       REG(0x40011000UL + 0x0C)

/* ---------- USART1 ---------- */
#define USART1_BASE     0x40013800UL
#define USART1_SR       REG(USART1_BASE + 0x00)
#define USART1_DR       REG(USART1_BASE + 0x04)
#define USART1_BRR      REG(USART1_BASE + 0x08)
#define USART1_CR1      REG(USART1_BASE + 0x0C)

/* ---------- bxCAN ---------- */
#define CAN_BASE        0x40006400UL
#define CAN_MCR         REG(CAN_BASE + 0x000)  /* dieu khien chinh */
#define CAN_MSR         REG(CAN_BASE + 0x004)  /* trang thai chinh */
#define CAN_RF0R        REG(CAN_BASE + 0x00C)  /* trang thai FIFO nhan 0 */
#define CAN_ESR         REG(CAN_BASE + 0x018)  /* bo dem loi TEC, REC */
#define CAN_BTR         REG(CAN_BASE + 0x01C)  /* bit timing */
#define CAN_RI0R        REG(CAN_BASE + 0x1B0)  /* ID cua frame trong FIFO 0 */
#define CAN_RDT0R       REG(CAN_BASE + 0x1B4)  /* DLC */
#define CAN_RDL0R       REG(CAN_BASE + 0x1B8)  /* data byte 0..3 */
#define CAN_RDH0R       REG(CAN_BASE + 0x1BC)  /* data byte 4..7 */
#define CAN_FMR         REG(CAN_BASE + 0x200)  /* che do cau hinh bo loc */
#define CAN_FM1R        REG(CAN_BASE + 0x204)  /* mask / list */
#define CAN_FS1R        REG(CAN_BASE + 0x20C)  /* 16 / 32 bit */
#define CAN_FFA1R       REG(CAN_BASE + 0x214)  /* bo loc -> FIFO nao */
#define CAN_FA1R        REG(CAN_BASE + 0x21C)  /* bat / tat bo loc */
#define CAN_F0R1        REG(CAN_BASE + 0x240)  /* bo loc 0: ID */
#define CAN_F0R2        REG(CAN_BASE + 0x244)  /* bo loc 0: mask */

/*
 * Chuyen clock he thong tu HSI (RC noi, sai so ~1%) sang HSE (thach anh
 * 8 MHz tren Blue Pill, sai so ~0.005%). CAN chi chiu duoc sai lech
 * clock khoang 0.5%, nen dung thach anh an toan hon nhieu.
 * Van la 8 MHz nen BRR (UART) va BTR (CAN) khong doi.
 */
#define RCC_CR      REG(0x40021000UL + 0x00)
#define RCC_CFGR    REG(0x40021000UL + 0x04)

void SystemInit(void)
{
    uint32_t timeout = 100000;

    RCC_CR |= (1UL << 16);                       /* HSEON = 1 */
    while ((RCC_CR & (1UL << 17)) == 0) {        /* cho HSERDY */
        if (--timeout == 0) {
            return;                              /* khong co thach anh: giu HSI */
        }
    }
    RCC_CFGR = (RCC_CFGR & ~3UL) | 1UL;          /* SW = 01: chon HSE */
    while (((RCC_CFGR >> 2) & 3UL) != 1UL) {     /* cho SWS = 01 */
    }
}

/* ================= UART (giong moc 1) ================= */
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

static void uart1_send_string(const char *s)
{
    while (*s != '\0') {
        uart1_send_char(*s);
        s++;
    }
}

/* In 1 so dang hex, 'digits' chu so. Vi du: print_hex(0x123, 3) -> "123" */
static void print_hex(uint32_t value, int digits)
{
    const char hex[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--) {
        uart1_send_char(hex[(value >> (i * 4)) & 0xF]);
    }
}

/* In so thap phan khong dau. Vi du: print_dec(25) -> "25" */
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

/* ================= LED ================= */
static void led_init(void)
{
    RCC_APB2ENR |= (1UL << 4);                 /* GPIOC */
    GPIOC_CRH &= ~(0xFUL << 20);
    GPIOC_CRH |=  (0x2UL << 20);               /* PC13: output */
}

/* ================= bxCAN ================= */
/* Tra ve 1 neu thanh cong, 0 neu bi treo o buoc nao do */
static int can_init(void)
{
    uint32_t timeout;

    /* 1. Bat clock: GPIOB + AFIO (APB2), CAN (APB1) */
    RCC_APB2ENR |= (1UL << 3) | (1UL << 0);
    RCC_APB1ENR |= (1UL << 25);

    /* 2. Remap CAN sang PB8/PB9: CAN_REMAP (bit 14:13) = 10 */
    AFIO_MAPR &= ~(3UL << 13);
    AFIO_MAPR |=  (2UL << 13);

    /* 3. PB8 (RX): input pull-up -> CNF 10, MODE 00 = 0x8, ODR8 = 1
     *    PB9 (TX): AF push-pull 50 MHz = 0xB */
    GPIOB_CRH &= ~(0xFFUL << 0);
    GPIOB_CRH |=  (0x8UL << 0) | (0xBUL << 4);
    GPIOB_ODR |=  (1UL << 8);

    /* 4. Thoat che do ngu (SLEEP = 0), xin vao che do khoi tao (INRQ = 1) */
    CAN_MCR &= ~(1UL << 1);
    CAN_MCR |=  (1UL << 0);
    timeout = 1000000;
    while ((CAN_MSR & (1UL << 0)) == 0) {      /* cho INAK = 1 */
        if (--timeout == 0) {
            return 0;
        }
    }

    /* 5. ABOM = 1: tu phuc hoi khi bi bus-off */
    CAN_MCR |= (1UL << 6);

    /* 6. Bit timing: SJW = 2, BS2 = 2, BS1 = 13, BRP = 1
     *    Moi truong ghi (gia tri - 1) */
    CAN_BTR = ((2UL - 1) << 24) | ((2UL - 1) << 20) | ((13UL - 1) << 16) | (1UL - 1);

    /* 7. Bo loc 0: che do mask 32-bit, ID = 0, mask = 0 -> nhan tat ca */
    CAN_FMR  |=  (1UL << 0);    /* FINIT = 1: vao che do cau hinh bo loc */
    CAN_FA1R &= ~(1UL << 0);    /* tat bo loc 0 truoc khi sua */
    CAN_FS1R |=  (1UL << 0);    /* 32-bit */
    CAN_FM1R &= ~(1UL << 0);    /* mask mode */
    CAN_F0R1 = 0;               /* ID */
    CAN_F0R2 = 0;               /* mask = 0: khong so bit nao -> nhan tat ca */
    CAN_FFA1R &= ~(1UL << 0);   /* dua vao FIFO 0 */
    CAN_FA1R |=  (1UL << 0);    /* bat bo loc 0 */
    CAN_FMR  &= ~(1UL << 0);    /* FINIT = 0 */

    /* 8. Roi che do khoi tao. bxCAN cho 11 bit lan lien tiep tren bus
     *    (bus ranh) roi moi xoa INAK -> can SN65HVD230 da co dien */
    CAN_MCR &= ~(1UL << 0);
    timeout = 1000000;
    while ((CAN_MSR & (1UL << 0)) != 0) {      /* cho INAK = 0 */
        if (--timeout == 0) {
            return 0;
        }
    }
    return 1;
}

static void can_print_frame(void)
{
    uint32_t ri  = CAN_RI0R;
    uint32_t dlc = CAN_RDT0R & 0xF;
    uint32_t lo  = CAN_RDL0R;
    uint32_t hi  = CAN_RDH0R;

    uart1_send_string("Nhan: ID=0x");
    if (ri & (1UL << 2)) {                       /* IDE = 1: ID mo rong 29 bit */
        print_hex(ri >> 3, 8);
    } else {                                     /* ID chuan 11 bit o bit 31:21 */
        print_hex(ri >> 21, 3);
    }
    uart1_send_string(" DLC=");
    print_dec(dlc);
    uart1_send_string(" data=");
    for (uint32_t i = 0; i < dlc; i++) {
        uint32_t byte = (i < 4) ? (lo >> (8 * i)) : (hi >> (8 * (i - 4)));
        print_hex(byte & 0xFF, 2);
        uart1_send_char(' ');
    }

    /* ESR: TEC o bit 23:16, REC o bit 31:24 */
    uart1_send_string("| TEC=");
    print_dec((CAN_ESR >> 16) & 0xFF);
    uart1_send_string(" REC=");
    print_dec((CAN_ESR >> 24) & 0xFF);
    uart1_send_string("\r\n");

    CAN_RF0R |= (1UL << 5);                      /* RFOM0: tra frame, FIFO dich len */
}

int main(void)
{
    led_init();
    uart1_init();
    uart1_send_string("\r\nBlue Pill - moc 3: nhan CAN 500 kbit/s\r\n");

    if (can_init()) {
        uart1_send_string("CAN init OK, dang cho frame...\r\n");
        GPIOC_ODR &= ~(1UL << 13);          /* LED SANG LIEN TUC = init OK */
    } else {
        uart1_send_string("Loi: CAN init bi treo (kiem tra SN65HVD230, PB8/PB9)\r\n");
        while (1) {
            GPIOC_ODR ^= (1UL << 13);       /* LED NHAY NHANH = init loi */
            for (volatile uint32_t i = 0; i < 100000; i++) {
            }
        }
    }

    uint32_t activity = 0;    /* CHAN DOAN: dem so lan PB8 doc ra 0 (bit troi) */

    while (1) {
        /* CHAN DOAN: PB8 = 0 nghia la module A dang thay bit troi tren bus.
         * ESP32 phat lai lien tuc -> LED se nhay khoang 2-3 lan/giay.
         * LED dung yen -> tin hieu khong toi duoc PB8. */
        if ((GPIOB_IDR & (1UL << 8)) == 0) {
            activity++;
            if (activity >= 200000) {
                GPIOC_ODR ^= (1UL << 13);
                activity = 0;
            }
        }

        /* FMP0 (bit 1:0) = so frame dang nam trong FIFO 0 */
        if ((CAN_RF0R & 0x3) != 0) {
            can_print_frame();
            GPIOC_ODR ^= (1UL << 13);
        }
    }
}
