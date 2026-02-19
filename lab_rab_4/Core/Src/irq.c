#include "irq.h"
#include "usart.h"

// we can choose 1 or 2 or 3rd operating mode: 0.1, 1, 10, default: 15Hz
// and for amplitude 1.2, 1.3, 1.6, def: 1.5
uint8_t dataLen = 2; // frequency, amplitude. What is amplitude if we have 0-3.3V interval? 0-3 for amplitude 1.5?
uint8_t dataCounter = 0;
volatile uint8_t isDataRead = 0;
volatile uint8_t buf[] = {'a', 'b'};
uint8_t buf2[] = {'a', 'b'};
void USART2_IRQHandler() {
	// receive
	if (USART2->SR & USART_SR_RXNE) {
		buf[dataCounter] = (uint8_t)(USART2->DR & 0xFF);
		// снять RXNE
		USART2->SR &= ~USART_SR_RXNE;
		dataCounter += 1; // считали ещё один бит
	}
	if (dataCounter >= dataLen) {
		isDataRead = 1;
		dataCounter=0;
		//transmit(buf2, 2);
	}
}

void TIM6_DAC_IRQHandler() {
	// отсчитал 1.5kHz частоту
	if (time < timer_cnt) {
		time = (time + 1) % timer_cnt;
	} else {
		time = 0;
	}
	timerUpdate = 1;
}

// was used previously instead of TIM6
void SysTick_Handler() {
	// отсчитал 1.5kHz частоту
	if (time < timer_cnt) {
		time = (time + 1) % timer_cnt;
	} else {
		time = 0;
	}
	timerUpdate=1;
}
