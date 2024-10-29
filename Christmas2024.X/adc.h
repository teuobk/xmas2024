#ifndef __ADC_H
#define __ADC_H

#include "global.h"

extern uint16_t gVcc;

uint8_t ADC_gen_entropy(void);
uint16_t ADC_read_vcc(void);
uint8_t ADC_random_int(void);
void ADC_set_random_state(uint8_t state);
uint8_t ADC_get_random_state(void);
uint8_t ADC_read_rf(void);
uint8_t ADC_read_vcc_fast(void);
        
#endif
