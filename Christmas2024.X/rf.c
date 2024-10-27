#include "rf.h"

#include "global.h"
#include "leds.h"
#include "adc.h"
#include "prefs.h"

// Macros and constants

#define RF_BARKER_SEQ  (0x0000FFF3UL)
#define RF_BARKER_MASK  (0x0000FFFFUL)
#define RF_NET_PAYLOAD_LEN_INC_ECC (8) // for an 8,4 Hamming code
#define RF_RAW_PAYLOAD_LEN_SAMPLES  (RF_NET_PAYLOAD_LEN_INC_ECC * 2) // for Manchester coding
#define RF_FRAME_BARKER_MASK (uint32_t)(RF_BARKER_MASK << RF_RAW_PAYLOAD_LEN_SAMPLES)
#define RF_FRAME_BARKER_SEQ  (uint32_t)(RF_BARKER_SEQ << RF_RAW_PAYLOAD_LEN_SAMPLES)

#define NUM_SAMPLES_TO_AVERAGE_FOR_SLICER   (8) // must be power of 2

// Typedefs

typedef enum
{
    // Zero is reserved, to guard against an all-zeros packet
            
    CMD_PWR_ULTRALOW = 1,
    CMD_PWR_NORM,
    CMD_PWR_HIGH,
    CMD_PWR_ULTRAHIGH,

    CMD_SUPERCAP_CHRG_DIS,
    CMD_SUPERCAP_CHRG_EN,

    CMD_TREE_STAR_DIS,
    CMD_TREE_STAR_EN,
            
    // 0x0F is also reserved, to guard against an all-ones packet

} rf_cmd_id_t;


// Variables

static uint8_t mRfLevelSamples[NUM_SAMPLES_TO_AVERAGE_FOR_SLICER] = {0}; // in 8-bit ADC counts relative to Vdd
static uint8_t mRfLevelIndex = 0; 
static uint8_t mRfLevelAverage = 0;
static uint8_t mRfLevelPeak = 0;
static uint32_t mBitCache = 0;
    
// Implementations

static bool rf_command_handler(uint8_t decodedWord)
{
    bool commandSuccess = true;
    prefs_t prefsTemp = gPrefsCache;

    switch (decodedWord)
    {
        case CMD_PWR_ULTRALOW:
            // Time limit 500 us, no harvest LED blinks
            prefsTemp.blinkTimeLimit = 1;
            prefsTemp.harvestBlinkEn = false;
            prefsTemp.harvestRailChargeEn = false;
            break;
        case CMD_PWR_NORM:
            // Time limit 1500 us, harvest LED blinks OK
            prefsTemp.blinkTimeLimit = 3;
            prefsTemp.harvestBlinkEn = true;
            prefsTemp.harvestRailChargeEn = false;
            break;
        case CMD_PWR_HIGH:
            // Time limit 3000 us, harvest LED blinks OK, drive harvest high-side
            prefsTemp.blinkTimeLimit = 6;
            prefsTemp.harvestBlinkEn = true;
            prefsTemp.harvestRailChargeEn = true;
            break;
        case CMD_PWR_ULTRAHIGH:
            // Time limit 5000 us, harvest LED blinks OK, blink every base system tick (20 Hz vs 10 Hz), drive harvest high-side
            prefsTemp.blinkTimeLimit = 10;
            prefsTemp.harvestBlinkEn = true;
            prefsTemp.harvestRailChargeEn = true;
            break;
        case CMD_SUPERCAP_CHRG_DIS:
            // Disable charging of the supercap
            prefsTemp.supercapChrgEn = false;
            break;
        case CMD_SUPERCAP_CHRG_EN:
            // Enable charging of the supercap
            prefsTemp.supercapChrgEn = true;
            break;
        case CMD_TREE_STAR_DIS:
            // Disable the tree star
            prefsTemp.treeStarEn = false;
            break;
        case CMD_TREE_STAR_EN:
            // Enable the tree star
            prefsTemp.treeStarEn = true;
            break;
        default:
            commandSuccess = false;
            break;
    }
        
    // Build multi-byte command
    
    if (commandSuccess)
    {
        PREFS_update(&prefsTemp);
    }
    
    return commandSuccess;
}

// Decode the frame, including correcting up to 1 flipped bit, using
// an [8,4] Hamming code
static bool rf_frame_decode(uint32_t frameBits)
{
    bool cmdSuccess = false;
    bool decodeSuccess = false;
    uint8_t decodedFrame = 0;
    
    // Extract the individual bits from the encoded byte
    // Done with ANDs of shifted literals to avoid rotations, which
    // must be done iteratively (one place at a time) on this architecture
    uint8_t p1 = !!(frameBits & (1 << 14)); 
    uint8_t p2 = !!(frameBits & (1 << 12));
    uint8_t d1 = !!(frameBits & (1 << 10));
    uint8_t p3 = !!(frameBits & (1 << 8));
    uint8_t d2 = !!(frameBits & (1 << 6));
    uint8_t d3 = !!(frameBits & (1 << 4));
    uint8_t d4 = !!(frameBits & (1 << 2));
    uint8_t p4 = frameBits & 1;

    // Calculate the syndrome using parity checks
    uint8_t s1 = p1 ^ d1 ^ d2 ^ d4;
    uint8_t s2 = p2 ^ d1 ^ d3 ^ d4;
    uint8_t s3 = p3 ^ d2 ^ d3 ^ d4;
    uint8_t s4 = p4 ^ d1 ^ d2 ^ d3;

    // Combine the syndrome bits into a single value
    uint8_t syndrome = (uint8_t)((s1 << 3) | (s2 << 2) | (s3 << 1) | s4);

    if (syndrome == 0) 
    {
        // No errors, extract the data nibble
        decodedFrame = (uint8_t)((d1 << 3) | (d2 << 2) | (d3 << 1) | d4);
        decodeSuccess = true;
    } 
    else if (syndrome > 0 && syndrome <= 8) 
    {
        // Single-bit error, correct it
        // (and avoid a lot more bit ops, since we only care about fixing data bits)
        if (syndrome == 3) // fix d1
        {
            d1 ^= 1;
        }
        else if (syndrome == 5) // fix d2
        {
            d2 ^= 1;
        }
        else if (syndrome == 6) // fix d3
        {
            d3 ^= 1;
        }
        else if (syndrome == 7) // fix d4
        {
            d4 ^= 1;
        }
        
//        frameBits ^= (1 << (16 - syndrome)); // Flip the erroneous bit
//
//        // Re-extract the corrected bits
//        d1 = !!(frameBits & (1 << 10));
//        d2 = !!(frameBits & (1 << 6));
//        d3 = !!(frameBits & (1 << 4));
//        d4 = !!(frameBits & (1 << 2));

        // Extract the corrected data nibble
        decodedFrame = (uint8_t)((d1 << 3) | (d2 << 2) | (d3 << 1) | d4);
        decodeSuccess = true;
    } 
    else 
    {
        // Uncorrectable error (more than 1 bit error)
    }
    
    // If successful decode, handle the command
    if (decodeSuccess)
    {
        cmdSuccess = rf_command_handler(decodedFrame);
    }
    
    return cmdSuccess;
}

// Sample the RF tap using the comparator
static uint8_t rf_read_comparator(void)
{
    uint8_t bitValue = 0;
    
    // Turn on the DAC
    DAC1CON0 = 0b10000000;
            
    // Output at the slicer level to the comparator, with the slicer
    // level taken as the highest level observed recently, divided by two, and
    // scaled to the 2**5 range of the DAC,
    // which is equivalent to dividing by 16 (or shifting right 4 times))
    DAC1CON1 = mRfLevelPeak >> 4; 
    
    // Set up comparator to sample RF tap on inverting input
    CM1NSEL = 0b0000;
    
    // Set up comparator to sample DAC on non-inverting input
    CM1PSEL = 0b0101;
            
    // Turn on the comparator with the output inverted
    CM1CON0 = 0b10010000;
    
    // Allow levels to settle (DAC in particular needs up to 10 us)
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    
    // Read comparator value
    bitValue = MC1OUT;
    
    // Turn off the comparator
    CM1CON0 = 0;
    
    // Turn off the DAC
    DAC1CON0 = 0;
    
    return bitValue;
}

// Sample another bit from the RF data tap and kick off command handling
// if it looks like we might have a command
void RF_sample_bit(void)
{ 
    uint8_t newBit = 0;
    
    // Sample the RF level with the comparator
    newBit = rf_read_comparator();
    
    // Push the new bit into the cache
    mBitCache = (uint32_t)(mBitCache << 1) | newBit;
    
    // Whenever the bit pattern shows a start sequence in a position consistent
    // with having received a full frame, attempt to decode the frame. 
    
    if ((mBitCache & RF_FRAME_BARKER_MASK) == RF_FRAME_BARKER_SEQ)
    {
        if (rf_frame_decode(mBitCache))
        {
            LED_blink_ack();
        }        
        else
        {
            LED_blink_nack();
        }
    }
}

// Check the Vrf level with the ADC so that we can set the slicer level
void RF_update_slicer_level(void)
{
    uint8_t rfPortCounts = ADC_read_rf();
    
    mRfLevelSamples[mRfLevelIndex] = rfPortCounts;
    mRfLevelIndex = (mRfLevelIndex + 1) % NUM_SAMPLES_TO_AVERAGE_FOR_SLICER;
    
    mRfLevelPeak = 0;
    
    for (uint8_t i = 0; i < NUM_SAMPLES_TO_AVERAGE_FOR_SLICER; i++)
    {
        uint8_t level = mRfLevelSamples[i];
        if (level > mRfLevelPeak)
        {
            mRfLevelPeak = level;
        }
    }    
}
