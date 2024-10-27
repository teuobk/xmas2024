#ifndef __GLOBAL_H
#define __GLOBAL_H

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

#define KEEP_ON_PIN         (uint8_t)(1 << 4) // on port C
#define SUPERCAP_CHRG_PIN   (uint8_t)(1 << 5) // on port C

#define TREE_STAR_PIN       (4) // on port B

// Systicks per second
#define TICKS_PER_SEC       (10)

typedef void (*func_t)(void);

extern uint32_t gTickCount; // absolute tick count

void TIMER_once(func_t pCallback, uint8_t halfMilliseconds);

#endif
