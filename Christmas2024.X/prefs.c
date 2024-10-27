#include "prefs.h"
#include "global.h"

// Macros and constants

#define PREFS_MAGIC_NUMBER      0xA8

typedef enum
{
    EEPROM_ADDR_FLAG,
    EEPROM_ADDR_BLINK_TIME,
    EEPROM_ADDR_MAGIC,
    EEPROM_ADDR__LEN
} eeprom_addrs_t;

#define EEPROM_FLAG_SUPERCAP_CHRG       0
#define EEPROM_FLAG_TREE_STAR           1
#define EEPROM_FLAG_HARVEST_CHRG        2
#define EEPROM_FLAG_HARVEST_BLINK       3

// Typedefs

// Variables

const prefs_t cDefaultPrefs = 
{
    .blinkTimeLimit = 3,
    
    .supercapChrgEn = true,
    .treeStarEn = false,
    .harvestRailChargeEn = true,
    .harvestBlinkEn = true,
    
    .magicNumber = PREFS_MAGIC_NUMBER,
};

// The actual underlying EEPROM storage location
__eeprom uint8_t mPrefsEepromBacking[EEPROM_ADDR__LEN];
//__eeprom uint8_t mPrefsEepromBacking0;
//__eeprom uint8_t mPrefsEepromBacking1;
//__eeprom uint8_t mPrefsEepromBacking2;

// A shadow copy of what's in the EEPROM so we can quickly tell what's changed
static prefs_t mPrefsEepromShadow;

// The globally accessible version of our preferences
prefs_t gPrefsCache;

// Implementations

// Load the cache directly from EEPROM
static void prefs_load(void)
{
    // Need to do this element by element
    gPrefsCache.blinkTimeLimit = mPrefsEepromBacking[EEPROM_ADDR_BLINK_TIME];
    
    uint8_t booleanFlags = mPrefsEepromBacking[EEPROM_ADDR_FLAG];
    
    gPrefsCache.harvestBlinkEn = !!(booleanFlags & (1 << EEPROM_FLAG_HARVEST_BLINK));
    gPrefsCache.supercapChrgEn = !!(booleanFlags & (1 << EEPROM_FLAG_SUPERCAP_CHRG));
    gPrefsCache.harvestRailChargeEn = !!(booleanFlags & (1 << EEPROM_FLAG_HARVEST_CHRG));
    gPrefsCache.treeStarEn = !!(booleanFlags & (1 << EEPROM_FLAG_TREE_STAR));
    
    gPrefsCache.magicNumber = mPrefsEepromBacking[EEPROM_ADDR_MAGIC];

}

// Write to the PIC16's internal EEPROM the specified value at the specified address
void PREFS_update(prefs_t* pProposedSettings)
{
    // WARNING: This is all slow!
    if (pProposedSettings->blinkTimeLimit != gPrefsCache.blinkTimeLimit)
    {
        gPrefsCache.blinkTimeLimit = pProposedSettings->blinkTimeLimit;
        
        mPrefsEepromBacking[EEPROM_ADDR_BLINK_TIME] = gPrefsCache.blinkTimeLimit;
    }
    
    if (pProposedSettings->harvestBlinkEn != gPrefsCache.harvestBlinkEn ||
        pProposedSettings->supercapChrgEn != gPrefsCache.supercapChrgEn ||
        pProposedSettings->harvestRailChargeEn != gPrefsCache.harvestRailChargeEn ||
        pProposedSettings->treeStarEn != gPrefsCache.treeStarEn)
    {
        gPrefsCache.harvestBlinkEn = pProposedSettings->harvestBlinkEn;
        gPrefsCache.supercapChrgEn = pProposedSettings->supercapChrgEn;
        gPrefsCache.harvestRailChargeEn = pProposedSettings->harvestRailChargeEn;
        gPrefsCache.treeStarEn = pProposedSettings->treeStarEn;
        
        uint8_t consolidatedFlags = (uint8_t)(
                gPrefsCache.harvestBlinkEn << EEPROM_FLAG_HARVEST_BLINK |
                gPrefsCache.supercapChrgEn << EEPROM_FLAG_SUPERCAP_CHRG |
                gPrefsCache.harvestRailChargeEn << EEPROM_FLAG_HARVEST_CHRG |
                gPrefsCache.treeStarEn << EEPROM_FLAG_TREE_STAR);
        
        mPrefsEepromBacking[EEPROM_ADDR_FLAG] = consolidatedFlags;
    }
        
    // Always update the magic number last in case earlier writes failed
    if (PREFS_MAGIC_NUMBER != gPrefsCache.magicNumber)
    {
        gPrefsCache.magicNumber = PREFS_MAGIC_NUMBER;
        mPrefsEepromBacking[EEPROM_ADDR_MAGIC] = PREFS_MAGIC_NUMBER;
    }
}

void PREFS_init(void)
{
    prefs_load();
    
    // Detect uninitialized prefs, and replace them with defaults
    // TODO: Also detect corrupt prefs
    if (gPrefsCache.magicNumber != PREFS_MAGIC_NUMBER)
    {
        PREFS_update((prefs_t*)&cDefaultPrefs);
    }            
}
