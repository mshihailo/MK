#include <inttypes.h>
#include "stm32f446xx.h"

extern volatile uint8_t isDataRead; // флаг UART
extern volatile uint32_t time; // time counter
extern volatile uint32_t timer_cnt; // до скольки считать time, период синуса в тактах
extern volatile uint8_t timerUpdate; // флаг TIM
extern volatile uint8_t buf[];
extern volatile double SIN_FREQ;
extern volatile double SIN_AMPLITUDE;
extern uint8_t buf2[];
