#ifndef __GLOBAL_H
#define __GLOBAL_H

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

#define LED_BACKDRIVE_PIN_1     (uint8_t)(1 << 0) // on port C
#define LED_BACKDRIVE_PIN_2     (uint8_t)(1 << 1) // on port C
#define LED_STOKER_PIN          (uint8_t)(1 << 3) // on port C
#define KEEP_ON_PIN             (uint8_t)(1 << 4) // on port C
#define SUPERCAP_MONITOR_PIN    (uint8_t)(1 << 5) // on port C
#define DEBUG_PIN               (uint8_t)(1 << 6) // on port C
#define SUPERCAP_MED_CHRG_PIN   (uint8_t)(1 << 7) // on port C

#define TREE_STAR_PIN       (4) // on port B

// Systicks per second
#define TICKS_PER_SEC       (20)

#define MIN(a,b)        ((a < b) ? (a) : (b))
#define MAX(a,b)        ((a > b) ? (a) : (b))

#define DEBUG_SET()         LATC = (LATC | DEBUG_PIN)
#define DEBUG_CLEAR()       LATC = (LATC & ~(DEBUG_PIN))
#define DEBUG_VALUE(_x)     LATC = (LATC & ~(DEBUG_PIN)) | ((!_x - 1) & (DEBUG_PIN))


const uint8_t cSetBitsInByte[256] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8
};


typedef void (*func_t)(void);


extern uint32_t gTickCount; // absolute tick count

void TIMER_once(func_t pCallback, uint8_t halfMilliseconds);

#endif
