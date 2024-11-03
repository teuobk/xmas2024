#ifndef __LEDS_H
#define __LEDS_H

#include "global.h"

void LED_twinkle(void);
void LED_blink_ack(void);
void LED_blink_nack(void);
void LED_show_charging(uint8_t chargeLevel);

#endif
