/* init.h
 * author: ttoomat
 * date: October, 13, 2025.
 */

#ifndef INC_INIT_H_
#define INC_INIT_H_

#include <inttypes.h>
#include "stm32f446xx.h"

#define CP_FREQ 100000000
#define SysTick_FREQ 1000
#define BAUDRATE 9600
#define APB1_FREQ 25000000

void RCC_Init();     // prescalers, clk
void DAC_Init();     // sine Analog output
void DAC2_Init();     // sine Analog output
void USART2_Init();  // for computer communication
void SysTick_Init(); // time for sine

#endif /* INC_INIT_H_ */
