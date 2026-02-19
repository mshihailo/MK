#include <math.h> // sin
#include "def.h"
#include "init.h"
#include "irq.h"
#include "usart.h"

#define CP_FREQ 100000000

volatile double SIN_FREQ = 10.0;
volatile double SIN_AMPLITUDE = 1.5;
volatile uint32_t timer_cnt = 1000;
volatile uint8_t timerUpdate = 0; // if timer updated, this flag will change
volatile uint32_t time = 0; // time counter
uint16_t maxDACData = 4095;


/* Для отправки данных нужен будет ещё один таймер
 *
 */

int main() {
	// 1. RCC, Prescalers
	RCC_Init();
	// 2. FPU on
	SCB->CPACR |= (3UL << 10 * 2) | (3UL << 11 * 2);
	// 3-6 - GPIO for UART + UART init + same for DAC
	USART2_Init();
	DAC_Init();
	//SysTick_Init();
	TIM6_Init();
	//ADC1_Init();
	while (1) {
		if (timerUpdate) {
			// upload to DAC
			double sin_value = SIN_AMPLITUDE/2.0 * (sin(M_PI/180.0 * time * SIN_FREQ) + 1.0);
			uint16_t DAC_value = (uint16_t)(sin_value * maxDACData / 2.975);
			DAC->DHR12R1 = DAC_value;
			timerUpdate = 0;
		}
		// if UART received data
		if (isDataRead) {
			USART_change_freq();
			isDataRead=0; // снимаем флаг, чтоб потом опять его ставить
		}
	}
}
