#include <stdint.h>

/* ===================== RCC ===================== */
#define PERIPHERAL_BASE      0x40000000U
#define AHB1_OFFSET          0x20000U
#define AHB1_BASE            (PERIPHERAL_BASE + AHB1_OFFSET)

#define RCC_OFFSET           0x3800U
#define RCC_BASE             (AHB1_BASE + RCC_OFFSET)
#define RCC_AHB1ENR_OFFSET   0x30U
#define RCC_AHB1ENR          (*(volatile uint32_t *)(RCC_BASE + RCC_AHB1ENR_OFFSET))

#define GPIOC_EN             (1U << 2)
#define GPIOD_EN             (1U << 3)

/* ===================== GPIO ===================== */
#define GPIOC_BASE           (AHB1_BASE + 0x800U)
#define GPIOD_BASE           (AHB1_BASE + 0xC00U)

#define MODER_OFFSET         0x00U
#define IDR_OFFSET           0x10U
#define ODR_OFFSET           0x14U
#define PUPDR_OFFSET         0x0CU

#define GPIOC_MODER          (*(volatile uint32_t *)(GPIOC_BASE + MODER_OFFSET))
#define GPIOC_ODR            (*(volatile uint32_t *)(GPIOC_BASE + ODR_OFFSET))
#define GPIOC_IDR            (*(volatile uint32_t *)(GPIOC_BASE + IDR_OFFSET))
#define GPIOC_PUPDR          (*(volatile uint32_t *)(GPIOC_BASE + PUPDR_OFFSET))

#define GPIOD_IDR            (*(volatile uint32_t *)(GPIOD_BASE + IDR_OFFSET))
#define GPIOD_PUPDR          (*(volatile uint32_t *)(GPIOD_BASE + PUPDR_OFFSET))

/* ===================== Pins ===================== */
#define LED_FIRST_PIN        4
#define LED_COUNT            8

#define BTN_RED_PIN          2   // PD2
#define BTN_BLUE_PIN         13  // PC13

/* ===================== Functions ===================== */
void gpio_init(void)
{
    /* Enable GPIOC and GPIOD */
    RCC_AHB1ENR |= GPIOC_EN | GPIOD_EN;

    /* PC4–PC11: output */
    for (int pin = LED_FIRST_PIN; pin < LED_FIRST_PIN + LED_COUNT; pin++) {
        GPIOC_MODER &= ~(3U << (pin * 2));
        GPIOC_MODER |=  (1U << (pin * 2));
    }

    /* Pull-up for buttons */
    GPIOC_PUPDR &= ~(3U << (BTN_BLUE_PIN * 2));
    GPIOC_PUPDR |=  (1U << (BTN_BLUE_PIN * 2));

    GPIOD_PUPDR &= ~(3U << (BTN_RED_PIN * 2));
    GPIOD_PUPDR |=  (1U << (BTN_RED_PIN * 2));
}

void delay(void)
{
    for (volatile uint32_t i = 0; i < 200000; i++);
}

/* ===================== Main ===================== */
int main(void)
{
    int pos = 0;
    gpio_init();

    while (1) {
        /* Red button: forward */
        if (!(GPIOD_IDR & (1U << BTN_RED_PIN))) {
            pos = (pos + 1) % LED_COUNT;
            while (!(GPIOD_IDR & (1U << BTN_RED_PIN)));
        }

        /* Blue button: backward */
        if (!(GPIOC_IDR & (1U << BTN_BLUE_PIN))) {
            pos = (pos + LED_COUNT - 1) % LED_COUNT;
            while (!(GPIOC_IDR & (1U << BTN_BLUE_PIN)));
        }

        /* Update LEDs */
        GPIOC_ODR &= ~(0xFFU << LED_FIRST_PIN);
        GPIOC_ODR |=  (1U << (LED_FIRST_PIN + pos));

        delay();
    }
}
