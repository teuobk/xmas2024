#include "rf.h"

#include "global.h"
#include "leds.h"
#include "adc.h"
#include "prefs.h"

// Macros and constants

#define RF_BARKER_SEQ     (0b1111111000000111UL)  // 11001 raw, with a prepended 1
#define RF_RAW_PAYLOAD_LEN (16)
#define RF_SAMPLES_PER_BIT  (3)
#define RF_SAMPLES_BIT_OFFSET  (0)
#define RF_RAW_PAYLOAD_LEN_SAMPLES  (RF_RAW_PAYLOAD_LEN * RF_SAMPLES_PER_BIT)
#define RF_MIN_CORR_FOR_CODEWORD_ACCEPT     (12) // based on the Hamming distance of the codewords being at worst 6 and generally 7 or better -- though admittedly, I'm not sure how best to set this threshold. This value seems to work well in practice.
#define NUM_SAMPLES_TO_AVERAGE_FOR_SLICER   (8) // must be power of 2

// Don't bother looking for RF traffic if the RF level isn't very high to begin with
#define RF_LEVEL_MIN_FOR_COMMS_COUNTS       (32)


// Command codewords
// These were chosen to have:
// * Low autocorrelation with +/- 1 bit timing shifts (resistance to false-positives due to timing errors)
// * High correlation between the first byte and the second byte (resilience to burst errors and bit flips)
// * Limited runs of 1s and 0s, no more than 3 1s or 2 0s in a row (long runs of 0s are problematic when powering the board from RF)
// * High Hamming distance, at least distance 4 between any two codewords (immunity to mismatches)
#define RF_CODEWORD_0       (0b1011001010110011)
#define RF_CODEWORD_1       (0b0100100001001010)
#define RF_CODEWORD_2       (0b1001010110010101)
#define RF_CODEWORD_3       (0b0101001101010011)
#define RF_CODEWORD_4       (0b0010010100100100)
#define RF_CODEWORD_5       (0b1110100111001101)
#define RF_CODEWORD_6       (0b0010101100110010)
#define RF_CODEWORD_7       (0b1110011010101001)
#define RF_CODEWORD__NUM    (8)

// Typedefs

typedef enum
{
    CMD_PWR_NORM = 0,
    CMD_PWR_ULTRAHIGH = 1,

    CMD_SUPERCAP_CHRG_DIS = 2,
    CMD_SUPERCAP_CHRG_EN = 3,

    CMD_TREE_STAR_DIS = 4,
    CMD_TREE_STAR_EN = 5,
            
    CMD_SELF_TEST = 6,
    CMD_UNLOCK = 7,
} rf_cmd_id_t;

typedef union 
{
    uint16_t    word;
    uint8_t     bytes[sizeof(uint16_t)];
} bytewise_16_t;

// Variables

static const uint16_t cCodewords[RF_CODEWORD__NUM] = 
{
    RF_CODEWORD_0,
    RF_CODEWORD_1,
    RF_CODEWORD_2,
    RF_CODEWORD_3,
    RF_CODEWORD_4,
    RF_CODEWORD_5,
    RF_CODEWORD_6,
    RF_CODEWORD_7,
};

static uint8_t mRfLevelSamples[NUM_SAMPLES_TO_AVERAGE_FOR_SLICER] = {0}; // in 8-bit ADC counts relative to Vdd
static uint8_t mRfLevelIndex = 0; 
static uint8_t mRfLevelAverage = 0;
static uint8_t mRfLevelPeak = 0;
static uint64_t mBitCache = 0;
    
static bool mCommandUnlocked = false;

// Implementations

// Compute the correlation between a and b, with 0s interpreted as -1s, using
// the given number of bytes for the calculation (counted from the LSB)
static int8_t rf_compute_correlation(uint16_t a, uint16_t b, uint8_t startByte, uint8_t endByte)
{
    int8_t corr = 0;
    bytewise_16_t pA;
    bytewise_16_t pB;

    pA.word = a;
    pB.word = b;

    // Compare from LSB to MSB, since we're big-endian
    for (uint8_t i = startByte; i <= endByte; i++)
    {
        uint8_t byte_a = pA.bytes[i];
        uint8_t byte_b = pB.bytes[i];

        uint8_t difference = byte_a ^ byte_b;
        uint8_t numDifferences = cSetBitsInByte[difference];
        corr += (8 - numDifferences);
    }

    return corr;
}


static bool rf_command_handler(uint8_t decodedWord)
{
    bool commandSuccess = true;
    prefs_t prefsTemp = gPrefsCache;

    switch (decodedWord)
    {
//        case CMD_PWR_ULTRALOW:
//            // Time limit 500 us, no harvest LED blinks
//            prefsTemp.blinkTimeLimit = 1;
//            prefsTemp.harvestBlinkEn = false;
//            prefsTemp.harvestRailChargeEn = false;
//            break;
        case CMD_PWR_NORM:
            // Time limit 1500 us, harvest LED blinks OK
            prefsTemp.blinkTimeLimit = 7; // MUST be a power of 2 minus 1
            prefsTemp.harvestBlinkEn = true;
            prefsTemp.harvestRailChargeEn = true;
            prefsTemp.fastBlinksEn = false;
            break;
//        case CMD_PWR_HIGH:
//            // Time limit 3000 us, harvest LED blinks OK, drive harvest high-side
//            prefsTemp.blinkTimeLimit = 7; // MUST be a power of 2 minus 1
//            prefsTemp.harvestBlinkEn = true;
//            prefsTemp.harvestRailChargeEn = true;
//            break;
        case CMD_PWR_ULTRAHIGH:
            // Time limit 5000 us, harvest LED blinks OK, drive harvest high-side
            prefsTemp.blinkTimeLimit = 31; // MUST be a power of 2 minus 1
            prefsTemp.harvestBlinkEn = true;
            prefsTemp.harvestRailChargeEn = true;
            prefsTemp.fastBlinksEn = true;
            break;
        case CMD_SUPERCAP_CHRG_DIS:
            if (mCommandUnlocked)
            {
                // Disable charging of the supercap
                prefsTemp.supercapChrgEn = false;
            }
            else
            {
                commandSuccess = false;
            }
            break;
        case CMD_SUPERCAP_CHRG_EN:
            if (mCommandUnlocked)
            {
                // Enable charging of the supercap
                prefsTemp.supercapChrgEn = true;
            }
            else
            {
                commandSuccess = false;
            }
            break;
        case CMD_TREE_STAR_DIS:
            // Disable the tree star
            prefsTemp.treeStarEn = false;
            break;
        case CMD_TREE_STAR_EN:
            // Enable the tree star
            prefsTemp.treeStarEn = true;
            break;
        case CMD_UNLOCK:
            // Enable special/restricted command on the next received frame only
            // Actual flag toggle comes after this block
            commandSuccess = false; // don't reveal it was a valid command
            break;
        case CMD_SELF_TEST:
            // Start a self-test
            // TODO
            break;            
        default:
            commandSuccess = false;
            break;
    }
    
    // Touch up the unlocked state
    if (decodedWord == CMD_UNLOCK)
    {
        mCommandUnlocked = true;
    }        
    else
    {
        mCommandUnlocked = false;
    }
    

    
    // Build multi-byte command
    
    if (commandSuccess)
    {
        PREFS_update(&prefsTemp);
    }
    
    return commandSuccess;
}



static bool rf_frame_decode(uint64_t frameBits)
{
    bool cmdSuccess = false;
    bool decodeSuccess = false;
    uint8_t decodedFrame = 0;
    
    // Extract the individual bits from the encoded byte
    // Done with ANDs of shifted literals to avoid rotations, which
    // must be done iteratively (one place at a time) on this architecture.
    // Also note how this has effectively been done as an unrolled loop, again to
    // avoid bitwise rotations
    
    uint16_t reconstructed = 0;
    
    // Each of these steps does some bit-flipping and such, all to get around the TOTAL CRAP that is the XC8 compiler.
    // Basically, we convert a set bit in the position of interest to a zero, then subtract 1 to convert to 0xFFFF,
    // then mask that with the bit we actually want to set. On the other hand, if the bit of interest started as 0, 
    // the logical-not will convert it to a 0x0001, so subtracting 1 will get 0, so we'll get a cleared bit in
    // our output position
    reconstructed |= (uint16_t)((uint16_t)(1UL << 15) & (uint16_t)(!(frameBits & (1ULL << (15*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    reconstructed |= (uint16_t)((uint16_t)(1UL << 14) & (uint16_t)(!(frameBits & (1ULL << (14*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    reconstructed |= (uint16_t)((uint16_t)(1UL << 13) & (uint16_t)(!(frameBits & (1ULL << (13*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    reconstructed |= (uint16_t)((uint16_t)(1UL << 12) & (uint16_t)(!(frameBits & (1ULL << (12*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    reconstructed |= (uint16_t)((uint16_t)(1UL << 11) & (uint16_t)(!(frameBits & (1ULL << (11*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    reconstructed |= (uint16_t)((uint16_t)(1UL << 10) & (uint16_t)(!(frameBits & (1ULL << (10*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    reconstructed |= (uint16_t)((uint16_t)(1UL << 9) & (uint16_t)(!(frameBits & (1ULL << (9*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    reconstructed |= (uint16_t)((uint16_t)(1UL << 8) & (uint16_t)(!(frameBits & (1ULL << (8*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    reconstructed |= (uint16_t)((uint16_t)(1UL << 7) & (uint16_t)(!(frameBits & (1ULL << (7*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    reconstructed |= (uint16_t)((uint16_t)(1UL << 6) & (uint16_t)(!(frameBits & (1ULL << (6*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    reconstructed |= (uint16_t)((uint16_t)(1UL << 5) & (uint16_t)(!(frameBits & (1ULL << (5*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    reconstructed |= (uint16_t)((uint16_t)(1UL << 4) & (uint16_t)(!(frameBits & (1ULL << (4*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    reconstructed |= (uint16_t)((uint16_t)(1UL << 3) & (uint16_t)(!(frameBits & (1ULL << (3*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    reconstructed |= (uint16_t)((uint16_t)(1UL << 2) & (uint16_t)(!(frameBits & (1ULL << (2*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    reconstructed |= (uint16_t)((uint16_t)(1UL << 1) & (uint16_t)(!(frameBits & (1ULL << (1*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    reconstructed |= (uint16_t)((uint16_t)(1UL << 0) & (uint16_t)(!(frameBits & (1ULL << (0*RF_SAMPLES_PER_BIT+RF_SAMPLES_BIT_OFFSET))) - 1)); 
    
    int8_t highestCorrelation = 0;
    uint8_t codewordWithHighestCorr = RF_CODEWORD__NUM;
    for (uint8_t i = 0; i < RF_CODEWORD__NUM; i++)
    {
        int8_t corr = rf_compute_correlation(reconstructed, cCodewords[i], 0, 1);
        if (corr > highestCorrelation)
        {
            highestCorrelation = corr;
            codewordWithHighestCorr = i;
        }
    }
    
    if (highestCorrelation >= RF_MIN_CORR_FOR_CODEWORD_ACCEPT)
    {
        cmdSuccess = rf_command_handler(codewordWithHighestCorr);
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
    
    // Allow levels to settle (DAC in particular needs up to 10 us) (Testing has 
    // shown that it definitely doesn't work with 10 nops, but seems to work
    // at 20 nops). Note that each loop is equivalent to about 10 instruction cycles
    for (uint8_t i = 0; i < 2; i++)
    {
        NOP();
    }

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
    // Don't bother sampling if there doesn't seem to be any RF energy around
    if (mRfLevelPeak < RF_LEVEL_MIN_FOR_COMMS_COUNTS)
    {
        return;
    }
    
    uint8_t newBit = 0;
    
    // Sample the RF level with the comparator
    newBit = rf_read_comparator();
    
    // Debug output
    
    
    // Push the new bit into the cache
    mBitCache = (uint64_t)(mBitCache << 1) | newBit;
    
    // Whenever the bit pattern shows a start sequence in a position consistent
    // with having received a full frame, attempt to decode the frame. 

    // Compute the correlation with the Barker code indicating the start of the frame
    int8_t barkerCorr = rf_compute_correlation((uint16_t)RF_BARKER_SEQ, (uint16_t)(mBitCache >> 48), 0, 1);

#define BARKER_CORR_THRESH  (14) // TBD
    if (barkerCorr > BARKER_CORR_THRESH)
    {
        if (rf_frame_decode(mBitCache))
        {
            LED_blink_ack();
        }        
        
        // Clear the cache to prevent duplicates, since some packets can look a bit like
        // another Barker start sequence
        mBitCache = 0;
    }
}

// Check the Vrf level with the ADC so that we can set the slicer level
uint8_t RF_update_slicer_level(void)
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
    
    return mRfLevelPeak;
}
