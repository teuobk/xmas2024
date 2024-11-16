#ifndef __PREFS_H
#define __PREFS_H

#include "global.h"

#define SELF_TEST_TIMEOUT_TICKS  (TICKS_PER_SEC * 3)

typedef struct  
{
    uint8_t     blinkTimeLimit;
    
    bool        treeStarEn;
    bool        harvestRailChargeEn;
    bool        harvestBlinkEn;
    bool        fastBlinksEn;
    
    bool        selfTestEn;
} prefs_t;



extern prefs_t gPrefsCache;

void PREFS_update(prefs_t* pProposedSettings);
void PREFS_self_test_saved_state(bool enable);
void PREFS_init(void);

#endif
