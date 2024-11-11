#ifndef __PREFS_H
#define __PREFS_H

#include "global.h"

typedef struct  
{
    uint8_t     blinkTimeLimit;
    
    bool        supercapChrgEn;
    bool        treeStarEn;
    bool        harvestRailChargeEn;
    bool        harvestBlinkEn;
    bool        fastBlinksEn;
} prefs_t;



extern prefs_t gPrefsCache;

void PREFS_update(prefs_t* pProposedSettings);
void PREFS_init(void);

#endif
