/*
 * Moc 1: STM32F103C8 (Blue Pill) in "Hello" qua UART - lap trinh thanh ghi truc tiep
 *
 * - Clock: dung HSI 8 MHz mac dinh sau reset (chua dung PLL cho don gian)
 * - USART1: PA9 = TX, PA10 = RX, 115200 baud, 8N1
 * - LED PC13: nhay de biet chuong trinh dang chay (LED sang khi chan = 0)
 *
 * Dia chi thanh ghi lay tu Reference Manual RM0008.
 */

#include <stdint.h>

/* Doc/ghi 1 thanh ghi 32-bit tai dia chi addr */
#define REG(addr)   (*(volatile uint32_t *)(addr))

/* ---------- RCC: bat clock cho ngoai vi ---------- */
#define RCC_BASE      0x40021000UL
#define RCC_APB2ENR   REG(RCC_BASE + 0x18)

#define RCC_IOPAEN    (1UL << 2)    /* clock cho GPIOA */
#define RCC_IOPCEN    (1UL << 4)    /* clock cho GPIOC */
#define RCC_USART1EN  (1UL << 14)   /* clock cho USART1 */

/* ---------- GPIO ---------- */
#define GPIOA_BASE    0x40010800UL
#define GPIOA_CRH     REG(GPIOA_BASE + 0x04)   /* cau hinh chan PA8..PA15 */

#define GPIOC_BASE    0x40011000UL
#define GPIOC_CRH     REG(GPIOC_BASE + 0x04)   /* cau hinh chan PC8..PC15 */
#define GPIOC_ODR     REG(GPIOC_BASE + 0x0C)   /* gia tri xuat ra chan */

/* ---------- USART1 ---------- */
#define USART1_BASE   0x40013800UL
#define USART1_SR     REG(USART1_BASE + 0x00)  /* thanh ghi trang thai */
#define USART1_DR     REG(USART1_BASE + 0x04)  /* thanh ghi du lieu */
#define USART1_BRR    REG(USART1_BASE + 0x08)  /* toc do baud */
#define USART1_CR1    REG(USART1_BASE + 0x0C)  /* thanh ghi dieu khien 1 */

#define USART_SR_TXE  (1UL << 7)    /* 1 = DR trong, ghi byte moi duoc */
#define USART_CR1_UE  (1UL << 13)   /* bat USART */
#define USART_CR1_TE  (1UL << 3)    /* bat bo phat */
#define USART_CR1_RE  (1UL << 2)    /* bat bo thu */

/*
 * Mot so file startup goi SystemInit() truoc main().
 * Ta khong cau hinh gi them (giu HSI 8 MHz), nen de ham rong.
 */
void SystemInit(void)
{
}

/* Delay "tho": chay vong lap rong. Khong chinh xac, chi de nhay LED. */
static void delay(volatile uint32_t count)
{
    while (count > 0) {
        count--;
    }
}

static void led_init(void)
{
    RCC_APB2ENR |= RCC_IOPCEN;

    /* PC13 nam trong CRH, 4 bit bat dau tu bit (13 - 8) * 4 = 20
     * 0x2 = MODE 10 (output 2 MHz), CNF 00 (push-pull) */
    GPIOC_CRH &= ~(0xFUL << 20);
    GPIOC_CRH |=  (0x2UL << 20);
}

static void uart1_init(void)
{
    /* 1. Bat clock cho GPIOA va USART1 */
    RCC_APB2ENR |= RCC_IOPAEN | RCC_USART1EN;

    /* 2. PA9 (TX): 0xB = MODE 11 (output 50 MHz), CNF 10 (alternate function push-pull)
     *    4 bit cua PA9 bat dau tu bit (9 - 8) * 4 = 4 */
    GPIOA_CRH &= ~(0xFUL << 4);
    GPIOA_CRH |=  (0xBUL << 4);

    /* 3. PA10 (RX): 0x4 = MODE 00 (input), CNF 01 (floating)
     *    4 bit cua PA10 bat dau tu bit (10 - 8) * 4 = 8 */
    GPIOA_CRH &= ~(0xFUL << 8);
    GPIOA_CRH |=  (0x4UL << 8);

    /* 4. Baud 115200 voi clock 8 MHz:
     *    USARTDIV = 8 000 000 / (16 * 115200) = 4.34
     *    phan nguyen = 4, phan le = 0.34 * 16 = 5.4 -> 5
     *    BRR = (4 << 4) | 5 = 0x45 */
    USART1_BRR = 0x45;

    /* 5. Bat bo phat, bo thu, roi bat USART */
    USART1_CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void uart1_send_char(char c)
{
    /* Cho den khi thanh ghi du lieu trong */
    while ((USART1_SR & USART_SR_TXE) == 0) {
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

int main(void)
{
    led_init();
    uart1_init();

    while (1) {
        uart1_send_string("Hello from Blue Pill!\r\n");

        GPIOC_ODR ^= (1UL << 13);   /* dao trang thai LED */
        delay(400000);              /* ~0.5 s voi clock 8 MHz (uoc luong) */
    }
}
