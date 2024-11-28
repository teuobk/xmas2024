#include <stddef.h>
#include "prefs.h"
#include "global.h"
#include "supercap.h"

// Macros and constants

#define PREFS_MAGIC_NUMBER      0x5E

typedef enum
{
    EEPROM_ADDR_FLAG,
    EEPROM_ADDR_BLINK_TIME,
    EEPROM_ADDR_SELF_TEST,
    EEPROM_ADDR__LEN
} eeprom_addrs_t;

#define EEPROM_FLAG_SUPERCAP_CHRG       0
#define EEPROM_FLAG_TREE_STAR           1
#define EEPROM_FLAG_HARVEST_CHRG        2
#define EEPROM_FLAG_HARVEST_BLINK       3
#define EEPROM_FLAG_FAST_BLINKS         4

// Different byte
#define EEPROM_FLAG_SELF_TEST           0

// Typedefs

// Variables

const prefs_t cDefaultPrefs = 
{
    .blinkTimeLimit = 7,  // MUST be a power of 2 minus 1
    
    .treeStarEn = false,
    .harvestRailChargeEn = true,
    .harvestBlinkEn = true,
    .fastBlinksEn = false,
    
    .selfTestEn = true,
};

// The actual underlying EEPROM storage location
__eeprom uint8_t mPrefsEepromBacking[EEPROM_ADDR__LEN];

// A shadow copy of what's in the EEPROM so we can quickly tell what's changed
static prefs_t mPrefsEepromShadow;

// The globally accessible version of our preferences
prefs_t gPrefsCache;

// Implementations

// Load the cache directly from EEPROM
static void prefs_load(void)
{
    uint8_t onesCount = 0;
    
    uint8_t blinkConfigRaw = mPrefsEepromBacking[EEPROM_ADDR_BLINK_TIME];
    
    onesCount = cSetBitsInByte[blinkConfigRaw];
    
    // We want odd parity
    if (onesCount & 1)
    {
        // Valid parity, so load the values
        gPrefsCache.blinkTimeLimit = blinkConfigRaw >> 2;
        gPrefsCache.fastBlinksEn = (blinkConfigRaw >> 1) & 1;
    }
    else
    {
        // Invalid parity, so use defaults
        gPrefsCache.blinkTimeLimit = cDefaultPrefs.blinkTimeLimit;
        gPrefsCache.fastBlinksEn = cDefaultPrefs.fastBlinksEn;
    }
    
    uint8_t booleanFlags = mPrefsEepromBacking[EEPROM_ADDR_FLAG];
    
    onesCount = cSetBitsInByte[booleanFlags];
    
    // We want odd parity
    if (onesCount & 1)
    {
        // Valid parity, so load the values
        gPrefsCache.harvestBlinkEn = !!(booleanFlags & (1 << (EEPROM_FLAG_HARVEST_BLINK + 1)));
        gPrefsCache.harvestRailChargeEn = !!(booleanFlags & (1 << (EEPROM_FLAG_HARVEST_CHRG + 1)));
        gPrefsCache.treeStarEn = !!(booleanFlags & (1 << (EEPROM_FLAG_TREE_STAR + 1)));
    }
    else
    {
        // Invalid parity, so use defaults
        gPrefsCache.harvestBlinkEn = cDefaultPrefs.harvestBlinkEn;
        gPrefsCache.harvestRailChargeEn = cDefaultPrefs.harvestRailChargeEn;
        gPrefsCache.treeStarEn = cDefaultPrefs.treeStarEn;
    }
    
    uint8_t selfTestFlag = mPrefsEepromBacking[EEPROM_ADDR_SELF_TEST];
    
    onesCount = cSetBitsInByte[selfTestFlag];
    
    // We want odd parity
    if (onesCount & 1)
    {
        // Valid parity, so load the values
        gPrefsCache.selfTestEn = !!(selfTestFlag & (1 << (EEPROM_FLAG_SELF_TEST + 1)));
    }
    else
    {
        // Invalid parity, so use defaults
        gPrefsCache.selfTestEn = cDefaultPrefs.selfTestEn;
    }
}

// Write to the PIC16's internal EEPROM the specified value at the specified address. Try 
// to limit the writes for particular commands to a single byte
void PREFS_update(prefs_t* pProposedSettings)
{
    // Force supercap charging to stop temporarily, as writing EEPROM takes a
    // while and requires a lot of power. Writing one byte takes about 3 ms.
    SUPERCAP_force_charging_off();
    
    // WARNING: Writes are very slow, about 2ms per byte!
    if (pProposedSettings->blinkTimeLimit != gPrefsCache.blinkTimeLimit ||
        pProposedSettings->fastBlinksEn != gPrefsCache.fastBlinksEn)
    {
        // No bounding checks here
        gPrefsCache.blinkTimeLimit = pProposedSettings->blinkTimeLimit;
        gPrefsCache.fastBlinksEn = pProposedSettings->fastBlinksEn;
        
        uint8_t writeValue = (uint8_t)(gPrefsCache.blinkTimeLimit << 1) | (gPrefsCache.fastBlinksEn & 1);
        
        uint8_t onesCount = cSetBitsInByte[writeValue];
        
        // Odd parity
        uint8_t parity = (onesCount & 1) ? 0 : 1;
        
        mPrefsEepromBacking[EEPROM_ADDR_BLINK_TIME] = (uint8_t)(writeValue << 1) | (parity & 1); 
    }
    
    if (pProposedSettings->harvestBlinkEn != gPrefsCache.harvestBlinkEn ||
        pProposedSettings->harvestRailChargeEn != gPrefsCache.harvestRailChargeEn ||
        pProposedSettings->treeStarEn != gPrefsCache.treeStarEn)
    {
        gPrefsCache.harvestBlinkEn = pProposedSettings->harvestBlinkEn;
        gPrefsCache.harvestRailChargeEn = pProposedSettings->harvestRailChargeEn;
        gPrefsCache.treeStarEn = pProposedSettings->treeStarEn;
        
        uint8_t consolidatedFlags = (uint8_t)(
                gPrefsCache.harvestBlinkEn << EEPROM_FLAG_HARVEST_BLINK |
                gPrefsCache.harvestRailChargeEn << EEPROM_FLAG_HARVEST_CHRG |
                gPrefsCache.treeStarEn << EEPROM_FLAG_TREE_STAR);
        
        // Odd parity
        uint8_t parity = (cSetBitsInByte[consolidatedFlags] & 1) ? 0 : 1;
        
        mPrefsEepromBacking[EEPROM_ADDR_FLAG] = (uint8_t)(consolidatedFlags << 1) | (parity & 1);
    }
}

// Enable or disable the saved self-test mode, but not the currently active one
// (so that self-test keeps running as long as desired). In other words, DON'T
// change gPrefsCache.selfTestEn here!
void PREFS_self_test_saved_state(bool enable)
{
    static uint8_t sLastTempSetState = UINT8_MAX;
    
    // Workaround since we can't initialize with a non-const value
    if (sLastTempSetState == UINT8_MAX)
    {
        sLastTempSetState = gPrefsCache.selfTestEn;
    }
    
    // Don't write the self-test flag repeatedly; once is enough
    if (enable != sLastTempSetState)
    {
        // Odd parity
        uint8_t parity = !enable; // in this case (with a bool), it's trivial
                
        mPrefsEepromBacking[EEPROM_ADDR_SELF_TEST] = (uint8_t)(enable << 1) | (parity & 1);
        sLastTempSetState = enable;
    }
}

// Read the preferences out of EEPROM, and initialize if needed
// Takes about 115 us when Fosc=16MHz, so long as no write is needed
void PREFS_init(void)
{
    prefs_load();
}
