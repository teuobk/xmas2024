#ifndef __RF_H
#define __RF_H

#include "global.h"

// Don't bother looking for RF traffic if the RF level isn't very high to begin with
#define RF_LEVEL_MIN_FOR_COMMS_COUNTS       (32)


void RF_sample_bit(void);
uint8_t RF_update_slicer_level(void);

#endif
