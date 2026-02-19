// ============================================================================
//  Основные заметки имени меня
//  - вопросы по TIM_EGR_UG, разрешению энкодеров, пределам скорости и т.п.
//    сохранены для дальнейшего исследования.
// ============================================================================

#include "stm32f446xx.h"      // заголовок периферии STM32F4 да почемуж ты не работаешь
#include <stdbool.h>
#include <stdint.h>

// ------------------------ параметры тактирования ---------------------------

#define HSE_SPEED                   8U                 /* частота внешнего источника (МГц) */
#define APB1_FREQ                   25000000U          /* частота шины APB1 после деления */
#define BAUDRATE                    115200U            /* скорость UART */

// ------------------------ UART / буфер передачи ----------------------------

#define TX_MAX_SIZE                 64U                /* размер буфера TX */
static volatile uint8_t txBuffer[TX_MAX_SIZE];
static volatile uint8_t txLength = 0;
static volatile uint8_t txPos = 0;
static volatile uint8_t txBusy = 0;

// ------------------------ флаги и состояние -------------------------------

volatile bool telemetryFlag = false;        // пора отправить телеметрию
volatile bool readMotorSpeedFLag = false;   // пора пересчитать скорость мотора

volatile int32_t prev_cnt_int  = 0;         // предыдущий счётчик внутреннего энкодера
volatile int32_t prev_cnt_ext  = 0;         // предыдущий счётчик внешнего энкодера
volatile int32_t current_motor_speed = 0;   // скорость мотора (разность счётчиков)

static float target_speed_percentage  = 0.0f;  // требуемое % скорости
static float current_speed_percentage = 0.0f;  // фактическое %
static float error                    = 0.0f;  // ошибка регулятора

// ------------------------ геометрия энкодеров -----------------------------

#define PPR_INT 100U      /* импульсов на канал внутреннего энкодера */
#define PPR_EXT 10U       /* рисок внешнего энкодера на оборот вала */
#define CPR_EXT 40        /* "цифровой" шаг внешнего энкодера */
#define MAX_COUNTS_ON_SPIN 400U /* макс. счётов таймера за 5 мс при 100% ШИМ */

// ------------------------ регулятор и расчёты ------------------------------

#define K_P 0.05f         /* пропорциональный коэффициент П-регулятора */
#define DESIRED_UART_ACCURACY 0.1f /*!< точность вывода по UART, 0.1→1 знак */

// макросы для расчёта PSC/ARR таймеров по желаемой частоте
#define TIM_PSC(PERIPH_FREQ, DESIRED_TIM_PSC_FREQ) ((PERIPH_FREQ/DESIRED_TIM_PSC_FREQ) - 1);
#define TIM_ARR(PERIPH_FREQ, TIM_PSC, DESIRED_TIM_FREQ) ((PERIPH_FREQ/(TIM_PSC + 1))/DESIRED_TIM_FREQ);

// желаемые частоты срабатывания прерываний от таймеров (Гц)
#define TIM6_FREQ                   200U
#define TIM5_FREQ                   200U
#define TIM4_FREQ                   1000U
#define TIM3_FREQ                   1000U
#define TIM2_FREQ                   1000U

// обрáтный абсолютный макрос (скалярный тип)
#define abs(x) ((x) < 0 ? -(x) : (x))

// ADC‑входы резисторов
#define INT_POT_ADC_NUM             10U                 /* внутренний потенциометр PC0 */
#define EXT_POT_ADC_NUM             8U                  /* внешний потенциометр PB0 */

// PLL‑настройки
#define PLLM                        (HSE_SPEED / 2U)    /* чтобы VCO_in = 2 МГц */
#define PLLN                        100U

// ---------------------------------------------------------------------------
//  Конфигурация тактирования и FLASH
// ---------------------------------------------------------------------------

static void configureRCC(void) {
    // включаем внешний кварц
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY));

    // делим шины APB1 и APB2 на 2 → 25 МГц
    RCC->CFGR &= ~(RCC_CFGR_PPRE1_2 | RCC_CFGR_PPRE1_1 | RCC_CFGR_PPRE1_0);
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV2;

    // настраиваем PLL: M, N, P и выбираем HSE как источник
    RCC->PLLCFGR &= ~(RCC_PLLCFGR_PLLM_Msk | RCC_PLLCFGR_PLLN_Msk |
                      RCC_PLLCFGR_PLLP_Msk | RCC_PLLCFGR_PLLSRC_Msk);
    RCC->PLLCFGR |= (PLLM << RCC_PLLCFGR_PLLM_Pos);
    RCC->PLLCFGR |= (PLLN << RCC_PLLCFGR_PLLN_Pos);
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLP_0;    // делитель 2 → P=2? (смотрите даташит)
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSE;

    // запускаем PLL и переключаемся на него
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_PLL);
}

static void configureFLASH(void) {
    // одна задержка на чтение при 50‑100 МГц
    FLASH->ACR &= ~FLASH_ACR_LATENCY;
    FLASH->ACR |= FLASH_ACR_LATENCY_1WS;
}

// ---------------------------------------------------------------------------
//  GPIO
// ---------------------------------------------------------------------------

static void configureGPIO(void) {
    // включаем тактирование портов A и B
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN |
                    RCC_AHB1ENR_GPIOBEN;

    // PA0/PA1 – каналы таймера 2 (внутренний энкодер)
    // PA2 – USART2_RX, PA3 – USART2_TX
    GPIOA->MODER &= ~(GPIO_MODER_MODER0_Msk | GPIO_MODER_MODER1_Msk |
                      GPIO_MODER_MODER2_Msk | GPIO_MODER_MODER3_Msk);
    GPIOA->MODER |= GPIO_MODER_MODER0_1 | GPIO_MODER_MODER1_1 |
                    GPIO_MODER_MODER2_1 | GPIO_MODER_MODER3_1;
    GPIOA->AFR[0] &= ~((0xF << (0*4)) | (0xF << (1*4)) |
                      (0xF << (2*4)) | (0xF << (3*4)));
    GPIOA->AFR[0] |= (7U << (2*4)) | (7U << (3*4));  // AF7: USART2
    GPIOA->AFR[0] |= (1U << (0*4)) | (1U << (1*4));  // AF1: TIM2
    GPIOA->PUPDR = (GPIOA->PUPDR & ~((3U << (2*2)) | (3U << (3*2))));

    // PB4 – TIM3_CH1 (ШИМ на ключ),
    // PB5 – выход направление мотора,
    // PB6/PB7 – TIM4_CH1/CH2 (внешний энкодер)
    GPIOB->MODER &= ~(GPIO_MODER_MODE4_Msk | GPIO_MODER_MODE5_Msk |
                      GPIO_MODER_MODE6_Msk | GPIO_MODER_MODE6_Msk);
    GPIOB->MODER |= GPIO_MODER_MODE4_1 | GPIO_MODER_MODE5_0 |
                    GPIO_MODER_MODE6_1 | GPIO_MODER_MODE7_1;
    GPIOB->AFR[0] &= ~((0xF << (4*4)) | (0xF << (6*4)) | (0xF << (7*4)));
    GPIOB->AFR[0] |= (2U << (4*4));                  // AF2: TIM3_CH1
    GPIOB->AFR[0] |= (2U << (6*4)) | (2U << (7*4));  // AF2: TIM4
    GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPD0_Msk |
                      GPIO_PUPDR_PUPD4_Msk |
                      GPIO_PUPDR_PUPD6_Msk |
                      GPIO_PUPDR_PUPD7_Msk);  // подтяжка вниз
}

// ---------------------------------------------------------------------------
//  DMA + USART2 (передача телеметрии)
// ---------------------------------------------------------------------------

static void configureDMA1(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    // сбрасываем все флаги потока 6
    DMA1->HIFCR = DMA_HIFCR_CTCIF6 | DMA_HIFCR_CHTIF6 |
                  DMA_HIFCR_CTEIF6 | DMA_HIFCR_CDMEIF6 |
                  DMA_HIFCR_CFEIF6;
    DMA1_Stream6->CR |= 4 << DMA_SxCR_CHSEL_Pos;  // канал 4 – USART2_TX
    DMA1_Stream6->PAR = (uint32_t)&USART2->DR;
    DMA1_Stream6->CR |= DMA_SxCR_DIR_0;           // память → периферия
    DMA1_Stream6->CR |= DMA_SxCR_MINC;            // инкремент указателя в памяти
    DMA1_Stream6->CR |= DMA_SxCR_TCIE;            // прерывание при окончании
    DMA1_Stream6->CR &= ~(DMA_SxCR_PSIZE_Msk);    // PSIZE = 8‑бит
}

static void configureUSART2(void) {
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    USART2->CR1 |= USART_CR1_IDLEIE;  // прерывание по состоянию IDLE
    USART2->CR3 |= USART_CR3_DMAT;    // включаем DMA для TX
    USART2->BRR = (APB1_FREQ/BAUDRATE);
    USART2->CR1 |= USART_CR1_TE | USART_CR1_UE;
}

// ---------------------------------------------------------------------------
//  Таймеры
// ---------------------------------------------------------------------------

static void configureTIM6(void) {
    RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;

    uint32_t TIM6_PSC = TIM_PSC(APB1_FREQ, 1000000U);
    TIM6->PSC = TIM6_PSC;                                 // 1‑МГц счёт
    TIM6->ARR = TIM_ARR(APB1_FREQ, TIM6_PSC, TIM6_FREQ);  // нужная периодика
    TIM6->DIER |= TIM_DIER_UIE;                           // прерывание по UIF
    TIM6->CR1 |= TIM_CR1_CEN;
}

static void configureTIM5(void) {
    RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
    (void)RCC->APB1ENR;

    TIM5->CR1 &= ~TIM_CR1_CEN;

    uint32_t TIM5_PSC = TIM_PSC(APB1_FREQ, 1000000U);
    TIM5->PSC = TIM5_PSC;
    TIM5->ARR = TIM_ARR(APB1_FREQ, TIM5_PSC, TIM5_FREQ);

    TIM5->EGR |= TIM_EGR_UG;      // вынудить обновление (сброс CNT)
    TIM5->SR  &= ~TIM_SR_UIF;
    TIM5->DIER |= TIM_DIER_UIE;

    TIM5->CR1 |= TIM_CR1_CEN;
}

void configureTIM4(void) {
    // внешний энкодер – 16‑бит таймер
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    uint32_t TIM4_PSC = TIM_PSC(APB1_FREQ, 1000000U);
    TIM4->PSC = TIM4_PSC;
    TIM4->ARR = TIM_ARR(APB1_FREQ, TIM4_PSC, TIM4_FREQ);

    // режим энкодера x4: считаем и ↑, и ↓
    TIM4->SMCR &= ~TIM_SMCR_SMS_Msk;
    TIM4->SMCR |= TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;
    TIM4->CCMR1 |= TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;
    TIM4->CR1 |= TIM_CR1_CEN;
}

static void configureTIM3(void) {
    // ШИМ‑канал для управления мотором (PB4)
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    TIM3->CR1 &= ~TIM_CR1_CEN;
    uint32_t TIM3_PSC = TIM_PSC(APB1_FREQ, 1000000U);
    TIM3->PSC = TIM3_PSC;
    TIM3->ARR = TIM_ARR(APB1_FREQ, TIM3_PSC, TIM3_FREQ);

    TIM3->CCMR1 &= ~(TIM_CCMR1_CC1S_Msk | TIM_CCMR1_OC1M_Msk);
    TIM3->CCMR1 |= (TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2);  // PWM1
    TIM3->CCMR1 |= TIM_CCMR1_OC1PE;  // предзагрузка CCR1

    TIM3->CCER |= TIM_CCER_CC1E;     // включаем вывод
    TIM3->CCER &= ~TIM_CCER_CC1P_Msk; // полярность прямая
    TIM3->CCR1 = 0;                  // мотор остановлен

    TIM3->EGR |= TIM_EGR_UG;         // обновляем PSC/ARR/CCR
    TIM3->CR1 |= TIM_CR1_CEN;
}

void configureTIM2(void) {
    // внутренний энкодер – 32‑бит таймер
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    uint32_t TIM2_PSC = TIM_PSC(APB1_FREQ, 1000000U);
    TIM2->PSC = TIM2_PSC;
    TIM2->ARR = TIM_ARR(APB1_FREQ, TIM2_PSC, TIM2_FREQ);

    TIM2->SMCR &= ~TIM_SMCR_SMS_Msk;
    TIM2->SMCR |= TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;  // режим x4
    TIM2->CCMR1 |= TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;
    TIM2->CR1 |= TIM_CR1_CEN;
}

// ---------------------------------------------------------------------------
//  NVIC
// ---------------------------------------------------------------------------

static void configureNVIC(void) {
    NVIC_SetPriority(TIM6_DAC_IRQn, 2);
    NVIC_SetPriority(TIM5_IRQn, 1);
    NVIC_SetPriority(DMA1_Stream6_IRQn, 0);

    NVIC_EnableIRQ(TIM5_IRQn);
    NVIC_EnableIRQ(DMA1_Stream6_IRQn);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

// ---------------------------------------------------------------------------
//  Вспомогательные функции
// ---------------------------------------------------------------------------

static int32_t get_motor_speed(void) {
    // вычисляем разность счётчика TIM2 за последний период
    int32_t current_cnt = (int32_t)TIM2->CNT;
    int32_t speed = current_cnt - prev_cnt_int;
    prev_cnt_int = current_cnt;
    return speed;
}

static int16_t get_ext_encoder_value(void) {
    // аналогично, но 16‑битный TIM4
    int16_t cnt = (int16_t)TIM4->CNT;
    int16_t difference = cnt - prev_cnt_ext;
    prev_cnt_ext = cnt;
    return difference;
}

static void control_step(void) {
    // читать внешний энкодер, ограничить по CPR_EXT
    int16_t delta = get_ext_encoder_value();
    if (delta > CPR_EXT) {
        delta = CPR_EXT;
    } else if (delta < -CPR_EXT) {
        delta = -CPR_EXT;
    }

    // изменить целевую скорость (%) в зависимости от поворота ручки
    target_speed_percentage += (float)delta / CPR_EXT;
    current_speed_percentage = (float)current_motor_speed / MAX_COUNTS_ON_SPIN;

    // пропорциональный регулятор
    error = target_speed_percentage - current_speed_percentage;
    float duty = error * K_P;

    // направление: low‑side ключ, значение направления на PB5
    if (duty < 0.0f) {
        GPIOB->BSRR = GPIO_BSRR_BS_5;  // вперёд
        duty = -duty;
    } else {
        GPIOB->BSRR = GPIO_BSRR_BR_5;  // назад
    }

    if (duty > 1.0f) duty = 1.0f;

    // "мёртвая зона": если скорость ≈ 0 и цель 0, убираем ШИМ
    if (current_speed_percentage < 0.01f &&
        current_speed_percentage > -0.01f &&
        target_speed_percentage == 0) duty = 0;

    // записываем скважность в CCR1
    TIM3->CCR1 = (uint32_t)(duty * (float)(TIM3->ARR + 1U));
}

// ---------------------------------------------------------------------------
//  Формирование строки телеметрии
// ---------------------------------------------------------------------------

static inline void send_char_to_tx(char c) {
    if (txLength < TX_MAX_SIZE) {
        txBuffer[txLength++] = (uint8_t)c;
    }
}

static inline void send_byte_to_tx(uint8_t b) {
    if (txLength < TX_MAX_SIZE) {
        txBuffer[txLength++] = b;
    }
}

static void send_int_to_tx(int16_t v) {
    char tmp[6];
    int i = 0;

    if (v < 0) {
        send_char_to_tx('-');
        v = -v;
    }

    if (v == 0) {
        send_char_to_tx('0');
        return;
    }

    while (v > 0) {                // конвертируем число в строку
        tmp[i++] = '0' + (v % 10);
        v /= 10;
    }
    while (i--) {
        send_char_to_tx(tmp[i]);
    }
}

static void send_telemetry(void) {
    // переводим значения в целые, учитывая точность
    int16_t target_int = (int16_t)(target_speed_percentage / DESIRED_UART_ACCURACY);
    int16_t actual_int = (int16_t)(current_speed_percentage / DESIRED_UART_ACCURACY);
    int16_t error_int  = (int16_t)(error / DESIRED_UART_ACCURACY);

    txLength = 0;
    txBusy = 1;
    txPos = 0;

    send_int_to_tx(target_int);
    send_char_to_tx(' ');
    send_int_to_tx(actual_int);
    send_char_to_tx(' ');
    send_int_to_tx(error_int);
    send_char_to_tx('\r');
    send_char_to_tx('\n');

    // запускаем DMA‑передачу
    DMA1_Stream6->NDTR = txLength;
    DMA1_Stream6->M0AR  = (uint32_t)txBuffer;
    DMA1_Stream6->CR  |= 0x1;  // включаем поток
}

// ---------------------------------------------------------------------------
//  main + обработчики прерываний
// ---------------------------------------------------------------------------

int main(void) {
    configureFLASH();
    SCB->CPACR |= (3UL << (10*2)) | (3UL << (11*2));  // включение FPU
    configureRCC();
    configureGPIO();
    configureTIM2();
    configureTIM3();
    configureTIM4();
    configureTIM5();
    configureTIM6();
    configureDMA1();
    configureUSART2();
    configureNVIC();

    while (1) {
        if (telemetryFlag) {                // пришло время слать телеметрию
            control_step();                 // рассчитать ШИМ
            if (!txBusy) {
                send_telemetry();
            }
            telemetryFlag = false;
        }
        if (readMotorSpeedFLag) {           // читать скорость мотора
            current_motor_speed = get_motor_speed();
            readMotorSpeedFLag = false;
        }
    }
}

void TIM5_IRQHandler(void) {
    if (TIM5->SR & TIM_SR_UIF) {
        telemetryFlag = true;
        TIM5->SR &= ~TIM_SR_UIF;
    }
}

void TIM6_DAC_IRQHandler(void) {
    if (TIM6->SR & TIM_SR_UIF) {
        readMotorSpeedFLag = true;
        TIM6->SR &= ~TIM_SR_UIF;
    }
}

void DMA1_Stream6_IRQHandler(void) {
    if ((DMA1->HISR & (0x1 << 21))) {      // флаг TC: передача UART закончена
        txBusy = false;
        DMA1->HIFCR |= (0x1 << 21);        // сброс флага
    }
}
