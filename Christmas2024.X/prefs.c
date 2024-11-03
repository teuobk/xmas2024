#include <stddef.h>
#include "prefs.h"
#include "global.h"

// Macros and constants

#define PREFS_MAGIC_NUMBER      0x5E

typedef enum
{
    EEPROM_ADDR_FLAG,
    EEPROM_ADDR_BLINK_TIME,
    EEPROM_ADDR_MAGIC,
    EEPROM_ADDR_CRC,
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
    .blinkTimeLimit = 7,  // MUST be a power of 2 minus 1
    
    .supercapChrgEn = true,
    .treeStarEn = false,
    .harvestRailChargeEn = true,
    .harvestBlinkEn = true,
    
    .magicNumber = PREFS_MAGIC_NUMBER,
    .crc = 0,
};

// CRC8 table for polynomial 0xEB, chosen because it has Hamming distance 5 for lengths up to 9 bytes
// (see https://users.ece.cmu.edu/~koopman/crc/crc8.html )
// NOTE: This is fine because we're protecting only 6 data bytes, but if we ever go beyond that,
// a different polynomial would be better.
static const uint8_t crc8_table[256] = {
    0x00, 0xEB, 0x3D, 0xD6, 0x7A, 0x91, 0x47, 0xAC, 0xF4, 0x1F, 0xC9, 0x22, 0x8E, 0x65, 0xB3, 0x58,
    0x03, 0xE8, 0x3E, 0xD5, 0x79, 0x92, 0x44, 0xAF, 0xF7, 0x1C, 0xCA, 0x21, 0x8D, 0x66, 0xB0, 0x5B,
    0x06, 0xED, 0x3B, 0xD0, 0x7C, 0x97, 0x41, 0xAA, 0xF2, 0x19, 0xCF, 0x24, 0x88, 0x63, 0xB5, 0x5E,
    0x05, 0xEE, 0x38, 0xD3, 0x7F, 0x94, 0x42, 0xA9, 0xF1, 0x1A, 0xCC, 0x27, 0x8B, 0x60, 0xB6, 0x5D,
    0x0C, 0xE7, 0x31, 0xDA, 0x76, 0x9D, 0x4B, 0xA0, 0xF8, 0x13, 0xC5, 0x2E, 0x82, 0x69, 0xBF, 0x54,
    0x0F, 0xE4, 0x32, 0xD9, 0x75, 0x9E, 0x48, 0xA3, 0xFB, 0x10, 0xC6, 0x2D, 0x81, 0x6A, 0xBC, 0x57,
    0x0A, 0xE1, 0x37, 0xDC, 0x70, 0x9B, 0x4D, 0xA6, 0xFE, 0x15, 0xC3, 0x28, 0x84, 0x6F, 0xB9, 0x52,
    0x09, 0xE2, 0x34, 0xDF, 0x73, 0x98, 0x4E, 0xA5, 0xFD, 0x16, 0xC0, 0x2B, 0x87, 0x6C, 0xBA, 0x51,
    0x18, 0xF3, 0x25, 0xCE, 0x62, 0x89, 0x5F, 0xB4, 0xEC, 0x07, 0xD1, 0x3A, 0x96, 0x7D, 0xAB, 0x40,
    0x1B, 0xF0, 0x26, 0xCD, 0x61, 0x8A, 0x5C, 0xB7, 0xEF, 0x04, 0xD2, 0x39, 0x95, 0x7E, 0xA8, 0x43,
    0x1E, 0xF5, 0x23, 0xC8, 0x64, 0x8F, 0x59, 0xB2, 0xEA, 0x01, 0xD7, 0x3C, 0x90, 0x7B, 0xAD, 0x46,
    0x1D, 0xF6, 0x20, 0xCB, 0x67, 0x8C, 0x5A, 0xB1, 0xE9, 0x02, 0xD4, 0x3F, 0x93, 0x78, 0xAE, 0x45,
    0x14, 0xFF, 0x29, 0xC2, 0x6E, 0x85, 0x53, 0xB8, 0xE0, 0x0B, 0xDD, 0x36, 0x9A, 0x71, 0xA7, 0x4C,
    0x17, 0xFC, 0x2A, 0xC1, 0x6D, 0x86, 0x50, 0xBB, 0xE3, 0x08, 0xDE, 0x35, 0x99, 0x72, 0xA4, 0x4F,
    0x12, 0xF9, 0x2F, 0xC4, 0x68, 0x83, 0x55, 0xBE, 0xE6, 0x0D, 0xDB, 0x30, 0x9C, 0x77, 0xA1, 0x4A,
    0x11, 0xFA, 0x2C, 0xC7, 0x6B, 0x80, 0x56, 0xBD, 0xE5, 0x0E, 0xD8, 0x33, 0x9F, 0x74, 0xA2, 0x49
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
    // Need to do this element by element
    gPrefsCache.blinkTimeLimit = mPrefsEepromBacking[EEPROM_ADDR_BLINK_TIME];
    
    uint8_t booleanFlags = mPrefsEepromBacking[EEPROM_ADDR_FLAG];
    
    gPrefsCache.harvestBlinkEn = !!(booleanFlags & (1 << EEPROM_FLAG_HARVEST_BLINK));
    gPrefsCache.supercapChrgEn = !!(booleanFlags & (1 << EEPROM_FLAG_SUPERCAP_CHRG));
    gPrefsCache.harvestRailChargeEn = !!(booleanFlags & (1 << EEPROM_FLAG_HARVEST_CHRG));
    gPrefsCache.treeStarEn = !!(booleanFlags & (1 << EEPROM_FLAG_TREE_STAR));
    
    gPrefsCache.magicNumber = mPrefsEepromBacking[EEPROM_ADDR_MAGIC];
    gPrefsCache.crc = mPrefsEepromBacking[EEPROM_ADDR_CRC];
}

static uint8_t prefs_calc_crc(uint8_t *data, uint8_t length) 
{
    uint8_t crc = 0;
    
    for (uint8_t i = 0; i < length; i++) 
    {
        crc = crc8_table[crc ^ data[i]];
    }
    
    return crc;
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
        
    if (PREFS_MAGIC_NUMBER != gPrefsCache.magicNumber)
    {
        gPrefsCache.magicNumber = PREFS_MAGIC_NUMBER;
        mPrefsEepromBacking[EEPROM_ADDR_MAGIC] = PREFS_MAGIC_NUMBER;
    }
    
    uint8_t newCrc = prefs_calc_crc((uint8_t*)&gPrefsCache, offsetof(prefs_t, crc));
    if (newCrc != gPrefsCache.crc)
    {
        gPrefsCache.crc = newCrc;
        mPrefsEepromBacking[EEPROM_ADDR_CRC] = newCrc;
    }
}

// Read the preferences out of EEPROM, and initialize if needed
// Takes about 115 us when Fosc=16MHz, so long as no write is needed
void PREFS_init(void)
{
    prefs_load();
    uint8_t expectedCrc = prefs_calc_crc((uint8_t*)&gPrefsCache, offsetof(prefs_t, crc));
    
    // Detect uninitialized or corrupted prefs, and replace them with defaults
    if (gPrefsCache.magicNumber != PREFS_MAGIC_NUMBER ||
        gPrefsCache.crc != expectedCrc)
    {
        PREFS_update((prefs_t*)&cDefaultPrefs);
    }            
}
