#include <stdint.h>
#include "initialization.h"

/*
  ОЖИДАЕМЫЙ ФОРМАТ КОМАНДЫ (n = 4 байта):
    buf[0] = команда: 'E'/'e', 'A'/'a', 'F'/'f', 'V'/'v', 'S'/'s'
    buf[1] = параметр (для V): '0'..'8', для остальных можно любой символ
    buf[2] = любой символ (может быть '\r' или просто мусор)
    buf[3] = любой символ (может быть '\n' или просто мусор)

  КОМАНДЫ:
    e/E : echo — отправить обратно принятые 4 байта
    a/A : включить все светодиоды PC4..PC11 + echo
    f/F : выключить все светодиоды PC4..PC11 + echo
    v/V : включить один светодиод по цифре '1'..'8' (PC4..PC11), '0' выключить все + echo
    s/S : отправить состояние светодиодов как 8 символов '0'/'1' (PC4..PC11)
*/

/* ------------------------- LED маски ------------------------- */
// Светодиоды занимают GPIOC биты 4..11
#define LEDS_MASK        (0x0FF0U)    // 0000 1111 1111 0000
#define LED_FIRST_PIN    4U
#define LED_COUNT        8U

/* ------------------------- RX буфер ------------------------- */
// Данные приходят по прерыванию USART2_IRQHandler,
// поэтому все переменные, которые меняются в ISR и читаются в main,
// должны быть volatile (иначе компилятор может “кэшировать”).
static volatile uint8_t isCommandRead = 0;
static volatile uint8_t counter = 0;
static volatile uint8_t buf[4];
static const uint8_t n = 4;

/* ------------------------- UART TX ------------------------- */
/*
  transmit: отправка массива байтов по USART2 (блокирующая).
  Она ждёт TXE (Transmit Data Register Empty) для каждого байта.
  TXE=1 означает: можно писать новый байт в DR.
*/
static void transmit(const uint8_t* data, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++)
    {
        // Пока TXE=0 — регистр данных занят, ждём.
        while ((USART2->SR & USART_SR_TXE) == 0U) { }

        // Записываем байт в DR — он уйдет на передачу.
        USART2->DR = data[i];
    }

    // По желанию можно дождаться полного окончания передачи (TC),
    // если важно, чтобы байты реально вышли в линию:
    // while ((USART2->SR & USART_SR_TC) == 0U) { }
}

/* Удобная отправка строки (без нулевого терминатора, просто как массив) */
static void transmit_str(const char* s)
{
    while (*s)
    {
        while ((USART2->SR & USART_SR_TXE) == 0U) { }
        USART2->DR = (uint8_t)(*s);
        s++;
    }
}

/* ------------------------- LED helpers ------------------------- */
/* Выключить все светодиоды PC4..PC11 */
static void leds_off(void)
{
    GPIOC->ODR &= ~LEDS_MASK;
}

/* Включить все светодиоды PC4..PC11 */
static void leds_on(void)
{
    GPIOC->ODR |= LEDS_MASK;
}

/*
  Включить один светодиод по индексу 0..7 (0->PC4, 7->PC11).
  Перед этим выключаем остальные (как “бегущий одиночный”).
*/
static void leds_one(uint8_t idx)
{
    leds_off();
    if (idx < LED_COUNT)
    {
        GPIOC->ODR |= (1U << (LED_FIRST_PIN + idx));
    }
}

/*
  Сформировать “снимок” светодиодов в виде 8 символов '0'/'1':
  res[0] соответствует PC4, res[7] соответствует PC11.
*/
static void leds_state_to_text(uint8_t res[8])
{
    uint32_t leds = GPIOC->ODR & LEDS_MASK;

    for (uint8_t i = 0; i < LED_COUNT; i++)
    {
        uint32_t pinMask = 1U << (LED_FIRST_PIN + i);
        res[i] = (leds & pinMask) ? (uint8_t)'1' : (uint8_t)'0';
    }
}

/* ------------------------- Variant handler ------------------------- */
/*
  Обработка команды V/v:
    data[1] = '0'..'8'
      '0' -> выключить все
      '1' -> PC4
      ...
      '8' -> PC11
*/
static void variant_handler(const uint8_t* data)
{
    // Всегда сначала выключаем все, чтобы включился ровно один (или ни одного)
    leds_off();

    // Если второй символ — цифра
    if (data[1] == (uint8_t)'0')
    {
        // Ничего не включаем — все выключены
        return;
    }

    if (data[1] >= (uint8_t)'1' && data[1] <= (uint8_t)'8')
    {
        uint8_t idx = (uint8_t)(data[1] - (uint8_t)'1'); // '1'->0 ... '8'->7
        leds_one(idx);
    }
    // Иначе — неверный параметр, просто оставим все выключенными
}

/* ------------------------- Command handler ------------------------- */
/*
  Основной обработчик команд.
  data[0] — команда, data[1] — параметр (для V).
*/
static void command_handler(const uint8_t* data, uint8_t len)
{
    switch (data[0])
    {
        /* Echo */
        case 'e':
        case 'E':
        {
            transmit(data, len);
            transmit_str("\r\n");
            break;
        }

        /* All leds ON */
        case 'a':
        case 'A':
        {
            leds_on();
            transmit(data, len);
            transmit_str("\r\n");
            break;
        }

        /* All leds OFF */
        case 'f':
        case 'F':
        {
            leds_off();
            transmit(data, len);
            transmit_str("\r\n");
            break;
        }

        /* Variant: one LED by number */
        case 'v':
        case 'V':
        {
            variant_handler(data);
            transmit(data, len);
            transmit_str("\r\n");
            break;
        }

        /* Show LED state */
        case 's':
        case 'S':
        {
            uint8_t res[8];
            leds_state_to_text(res);
            transmit(res, 8);
            transmit_str("\r\n");
            break;
        }

        /* Unknown command */
        default:
        {
            transmit_str("ERR\r\n");
            break;
        }
    }
}

/* ------------------------- USART2 IRQ handler ------------------------- */
/*
  Прерывание по USART2.
  Мы включили RXNEIE (прерывание по приёму), поэтому при каждом пришедшем байте
  срабатывает этот обработчик.

  Важно:
  - RXNE сбрасывается чтением DR (поэтому мы НЕ пишем в SR).
  - Чтобы не потерять байты, читаем DR быстро.
*/
void USART2_IRQHandler(void)
{
    // Проверка, что действительно пришли данные (RXNE=1)
    if ((USART2->SR & USART_SR_RXNE) != 0U)
    {
        // Чтение DR:
        // - возвращает байт
        // - автоматически сбрасывает RXNE
        uint8_t b = (uint8_t)(USART2->DR & 0xFFU);

        // Сохраняем в буфер
        buf[counter] = b;
        counter++;

        // Если набрали 4 байта — помечаем, что команда готова
        if (counter >= n)
        {
            isCommandRead = 1;
            counter = 0;
        }
    }

    // Можно дополнительно обрабатывать ошибки (ORE, FE, NE),
    // но для лабораторной часто не требуют.
}

/* ------------------------- main ------------------------- */
int main(void)
{
    // Инициализация пинов UART (PA2/PA3 AF7), включение GPIOA clock и т.п.
    GPIOA_Init();

    // Инициализация USART2: baudrate, TE/RE, RXNEIE, NVIC_EnableIRQ, UE=1
    USART2_Init();

    // Настройка PC4..PC11 как OUTPUT
    LED_mode_setup();

    // В начале выключим всё
    leds_off();

    // Главный цикл
    while (1)
    {
        // Если ISR сообщил, что команда прочитана
        if (isCommandRead)
        {
            // Копируем volatile буфер в обычный (чтобы ISR не “мешал” при обработке)
            uint8_t local[4];
            for (uint8_t i = 0; i < n; i++)
                local[i] = buf[i];

            // Сбрасываем флаг ДО обработки (безопаснее)
            isCommandRead = 0;

            // Обрабатываем команду
            command_handler(local, n);
        }
    }
}
