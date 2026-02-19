#include "init.h"
#define TIM6_DIER_UIE 0 // Update interrupt enable bit

void ADC1_GPIO_Init() {
	// ADC_IN1 = PC2
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
	// PC2 Analog
	GPIOC->MODER |= (1U << 4);
	GPIOC->MODER |= (1U << 5);
}

/* Посмотреть на схеме, какой канал ADC. Настроить MODER для этого пина.
 * AnalogIN1 = PC2
 */
void ADC1_Init() {
	NVIC_EnableIRQ(ADC_IRQn);
	// настройка GPIO
	ADC1_GPIO_Init();
	// Включение тактирования АЦП
	RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
	// Настройка последовательности преобразований АЦП
	// 1. Enable ADC (ADON)
	ADC1->CR2 |= (1U << 0);
	while (!(ADC1->CR2 & ADC_CR2_ADON)) {}
	// 2. Set continuous conversation flag
	ADC1->CR2 |= (1U << 1);
	// EOCIE - прерывание "Regular channel end of conversion"
	ADC1->CR1 |= (1U << 5);
	// 3. write channel number in SQR3 - only one channel
	ADC1->SQR3 = 12; // 12 = ADC channel for PC2
	// Длина последовательности = 1 преобразование
	ADC1->SQR1 &= ~ADC_SQR1_L; // L = 0 -> 1 conversion
	// 4. Start conversation SWSTART
	ADC1->CR2 |= (1U << 30);
	// 5. Wait end of conversation (flag or IRQ)
	while (!(ADC1->SR & ADC_SR_EOC)) {}
	// Установка длины последовательности АЦП
	// Включение АЦП
}

void RCC_Init() {
	// 1. Set FLASH latency
	// flash latency 3 wait states - table 5 Ref Man
	FLASH->ACR |= FLASH_ACR_LATENCY_3WS;
	// 2. Enable HSE (external clock), wait until HSE is ready
	RCC->CR |= RCC_CR_HSEON;
	while(!(RCC->CR & RCC_CR_HSERDY)) {}
	// 3. Set AHB, APB1, APB2 Prescaler
	// AHB Prescaler = 1
	RCC->CFGR &= ~RCC_CFGR_HPRE_3;
	// APB1 Presc = 4 (101)
	RCC->CFGR |= RCC_CFGR_PPRE1_2;
	RCC->CFGR &= ~RCC_CFGR_PPRE1_1;
	RCC->CFGR |= RCC_CFGR_PPRE1_0;
	// APB2 Presc = 4 (101)
	RCC->CFGR |= RCC_CFGR_PPRE2_2;
	RCC->CFGR &= ~RCC_CFGR_PPRE2_1;
	RCC->CFGR |= RCC_CFGR_PPRE2_0;
	// 4. Config PLL
	// 4.1. Set PLL clock source
	RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSE;
	// 4.2. Set PLL M, N and P prescaler
	// M = 8 => 001000
	RCC->PLLCFGR |= RCC_PLLCFGR_PLLM_3;
	RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLM_0;
	RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLM_1;
	RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLM_2;
	RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLM_4;
	RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLM_5;
	// N = 100 =>  001100100
	RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLN_8;
	RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLN_7;
	RCC->PLLCFGR |= RCC_PLLCFGR_PLLN_6;
	RCC->PLLCFGR |= RCC_PLLCFGR_PLLN_5;
	RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLN_4;
	RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLN_3;
	RCC->PLLCFGR |= RCC_PLLCFGR_PLLN_2;
	RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLN_1;
	RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLN_0;
	// P = 2 => 00
	RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLP_1;
	RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLP_0;
	// 4.3. Enable PLL. Wait until PLL start
	RCC->CR |= RCC_CR_PLLON;
	while(!(RCC->CR & RCC_CR_PLLRDY)) {}
	// 5. Set Main clock switch to PLL source
	// PLL_P => 10 (System clock switch)
	RCC->CFGR |= RCC_CFGR_SW_1;
	RCC->CFGR &= ~RCC_CFGR_SW_0;
	// check that the switch moves to correct source - wait
	while (!(RCC->CFGR & (RCC_CFGR_SWS_1 & (~RCC_CFGR_SWS_0)))) {}
}

void DAC_MODER_Init() {
	// PA4
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
	// PA4 mode: Analog
	GPIOA->MODER |= (1U << 8);
	GPIOA->MODER |= (1U << 9);
}

void DAC_Init() {
	DAC_MODER_Init();
	RCC->APB1ENR |= RCC_APB1ENR_DACEN;
	// channel 1
	DAC->CR |= DAC_CR_EN1;
	// upload data to DAC
	DAC->DHR12R1 = 0xFFF;
}

void USART2_MODER_Init() {
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
	// USART2
	// PA2 mode: AF (10)
	GPIOA->MODER |= (1U << 5);
	GPIOA->MODER &= ~(1U << 4);
	// PA3 mode: AF (10)
	GPIOA->MODER |= (1U << 7);
	GPIOA->MODER &= ~(1U << 6);

	// PA2 AFRL=AFR[0] 0111: AF07 (USART2_TX)
	GPIOA->AFR[0] &= ~(1U << 11);
	GPIOA->AFR[0] |= (1U << 10);
	GPIOA->AFR[0] |= (1U << 9);
	GPIOA->AFR[0] |= (1U << 8);
	// PA3 AF: AF07 (0111) (USART2_RX)
	GPIOA->AFR[0] &= ~(1U << 15);
	GPIOA->AFR[0] |= (1U << 14);
	GPIOA->AFR[0] |= (1U << 13);
	GPIOA->AFR[0] |= (1U << 12);
}

void USART2_Init() {
	USART2_MODER_Init();
	RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
	// transmit enable, receive enable
	USART2->CR1 |= USART_CR1_TE;
	USART2->CR1 |= USART_CR1_RE;
	// baudrate
	USART2->BRR = APB1_FREQ / BAUDRATE;
	// RXNE interrupt enable
	USART2->CR1 |= USART_CR1_RXNEIE;
	// MVIC USART2 interrupt function
	NVIC_EnableIRQ(USART2_IRQn);
	// USART2 enable
	USART2->CR1 |= USART_CR1_UE;
}

// base timer
void TIM6_Init() {
	// вкл тактирование таймера, APB1
	RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
	// у base timer нет gpio
	// APB1 Prescaler = 4 из RCC init
	// настройка Prescaler, ARR
	// TIM6_FREQ = APB1_TIMER_FREQ/(PSC+1)(ARR+1)
	// (PSC+1)(ARR+1) = 50000000/2000=25000=500*50
	TIM6->PSC = (uint32_t)(499);
	TIM6->ARR = (uint32_t)(49);
	TIM6->DIER |= TIM6_DIER_UIE; // Update event interrupt enable
	NVIC_EnableIRQ(TIM6_DAC_IRQn); // настроить прерывания
	TIM6->CR1 |= TIM_CR1_CEN; // включить счётчик
}

void SysTick_Init() {
  // до скольки он считает
  SysTick->LOAD = (uint32_t)(CP_FREQ/SysTick_FREQ - 1);
  // processor clock
  SysTick->CTRL |= (1U << 2);
  // exception when counts down zero
  SysTick->CTRL |= (1U << 1);
  // Enable
  SysTick->CTRL |= (1U << 0);
  NVIC_EnableIRQ(SysTick_IRQn);
}
