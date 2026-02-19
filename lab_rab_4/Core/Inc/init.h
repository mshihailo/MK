/* init.h
 * author: ttoomat
 * date: November, 10, 2025.
 */

#ifndef INC_INIT_H_
#define INC_INIT_H_

#include "def.h"

// желаемые частоты на таймерах:
#define SysTick_FREQ 1000
#define TIM6_FREQ    2000
// желаемая скорость передачи UART
#define BAUDRATE 9600
// при настройке RCC получены частоты:
#define CP_FREQ   100000000
#define APB1_FREQ 25000000
#define APB1_TIMER_FREQ 50000000

void RCC_Init();     // prescalers, clk
void DAC_Init();     // sine Analog output
void USART2_Init();  // for computer communication
void SysTick_Init(); // it used to be time for sin
void TIM6_Init();    // time for sine
void ADC1_Init();

#endif /* INC_INIT_H_ */
