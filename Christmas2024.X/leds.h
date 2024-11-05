#ifndef __LEDS_H
#define __LEDS_H

#include "global.h"

#define LED_BLINK_LOW_THRESH_MV                 2400 // When the voltage is below this level, the situation is considered "low power" so the low time limit applies no matter the power mode

void LED_twinkle(void);
void LED_blink_ack(void);
void LED_show_power(uint8_t powerLevel);

#endif
