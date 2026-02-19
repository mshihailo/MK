#include "def.h"
#include "usart.h"
#include "irq.h"

// transmit an array
void transmit(const uint8_t* data, uint8_t n) {
	for (uint8_t i = 0; i < n; i++) {
		// пока TXE = 0 будет задержка (TXE = 1 means USART_DR is empty, we can write)
		while (!(USART2->SR & USART_SR_TXE)) {};
		USART2->DR = data[i];
	}
}

void USART_change_freq() {
	transmit(buf, 2);
	// frequency
	switch (buf[0]) {
		case '1': {
			SIN_FREQ = 5.0;
			break;
		}
		case '2': {
			SIN_FREQ = 100.0;
			break;
		}
		case '3': {
			SIN_FREQ = 10.0;
			break;
		}
		default: {
			SIN_FREQ = 15.0;
			break;
		}
	}
	switch (buf[1]) {
		case '1': {
			SIN_AMPLITUDE = 1.0;
			break;
		}
		case '2': {
			SIN_AMPLITUDE = 2.0;
			break;
		}
		case '3': {
			SIN_AMPLITUDE = 0.5;
			break;
		}
		default: {
			SIN_AMPLITUDE = 1.5;
			break;
		}
	}
	timer_cnt = 100000.0 * SIN_FREQ;
}
