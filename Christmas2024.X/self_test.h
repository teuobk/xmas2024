#ifndef __SELF_TEST_H
#define __SELF_TEST_H

#include "global.h"

#define SELF_TEST_TIMEOUT_TICKS  (TICKS_PER_SEC * 30)

typedef enum
{
    STS_USB_LDO,
    STS_SUPERCAP,
    STS_RADIO,
    STS_COMPLETE,
    STS__NUM
} self_test_step_t;

self_test_step_t SELF_TEST_get_current_step(void);
void SELF_TEST_state_machine_update(void);

#endif
