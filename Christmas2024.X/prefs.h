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
    
    uint8_t     magicNumber; // to detect uninitialized prefs
    uint8_t     crc; // protects all of the preferences
} prefs_t;



extern prefs_t gPrefsCache;

void PREFS_update(prefs_t* pProposedSettings);
void PREFS_init(void);

#endif
