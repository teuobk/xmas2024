#ifndef __GLOBAL_H
#define __GLOBAL_H

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

#define LED_BACKDRIVE_PIN_1     (uint8_t)(1 << 0) // on port C
#define LED_BACKDRIVE_PIN_2     (uint8_t)(1 << 1) // on port C
#define LED_STOKER_PIN          (uint8_t)(1 << 3) // on port C
#define KEEP_ON_PIN             (uint8_t)(1 << 4) // on port C
#define SUPERCAP_CHRG_PIN       (uint8_t)(1 << 5) // on port C
#define DEBUG_PIN               (uint8_t)(1 << 6) // on port C
#define SUPERCAP_MED_CHRG_PIN   (uint8_t)(1 << 7) // on port C

#define TREE_STAR_PIN       (4) // on port B

// Systicks per second
#define TICKS_PER_SEC       (20)

#define MIN(a,b)        ((a < b) ? (a) : (b))
#define MAX(a,b)        ((a > b) ? (a) : (b))



typedef void (*func_t)(void);

extern uint32_t gTickCount; // absolute tick count

void TIMER_once(func_t pCallback, uint8_t halfMilliseconds);

#endif
