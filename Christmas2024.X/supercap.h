#ifndef __SUPERCAP_H
#define __SUPERCAP_H

#include "global.h"

bool SUPERCAP_charge(void);
void SUPERCAP_force_charging_off(void);
uint8_t SUPERCAP_get_latest_voltage_delta(void);

#endif
