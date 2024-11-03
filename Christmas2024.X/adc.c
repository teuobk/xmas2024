#include "adc.h"
#include "global.h"


// Macros and constants

// Typedefs

// Variables

static uint8_t mRandomState; // Most recent PRNG state
static uint8_t mRandomSeed; // Most recently set seed

uint16_t gVcc = 0;

// Implementations


// Get a source of entropy by reading the temperature sensor at high speed,
// except guarantee it is non-zero
uint8_t ADC_gen_entropy(void)
{
    // Set ADC clock to Fosc (so that we get a noisy result and don't have to turn on another oscillator),
    // results right-justified (we want the noisy bits), and turn the module on
    ADCON0 = 0b10000100;
    
    // Enable temperature diodes in low range
    TSEN = 1;
    
    // Set source to temperature diodes
    ADPCH = 0b111101;
            
    // Start conversion without waiting much. We're just using this for entropy,
    // not the actual temperature.
    ADACQ = 254; // about 15 us
    ADGO = 1;
    
    // Wait for completion
    while (ADGO);
    
    // Turn off ADC
    ADON = 0;
    
    // Disable temperature source
    TSEN = 0;
    
    return ADRESL;
}

// Read Vcc in tens of mV
uint16_t ADC_read_vcc(void)
{
    // Constant for computing VCC in millivolts from an 8-bit ADC conversion 
    // of the 1024 mV FVR with VDD as positive reference
    #define VCC_MV_FROM_FVR_EIGHT_BIT ((uint32_t)1024 * 255)

    uint16_t mv = 0;
    
    // Set ADC clock to internal (FRC), results left-justified
    ADCON0 = 0b00010000;
    
    // Set the measured channel to FVR and reference to Vdd
    ADPCH = 0b111111;
    ADREF = 0b00000000;
    
    // Set acquisition time to 10 ADC clocks (10 us)
    ADACQ = 10;
    
    // Turn on FVR at 1024 mV for the ADC and wait for it to stabilize
    FVRCON = 0b10000001;
    while (!FVRRDY);
    
    // Turn on ADC
    ADON = 1;
    
    // Start conversion
    ADGO = 1;
    
    // Wait for completion
    while (ADGO);
    
    // Turn off ADC
    ADON = 0;
    
    // Turn off FVR and buffer
    FVRCON = 0b00000000;
    
    // Convert the value into millivolts
    mv = (uint16_t)(VCC_MV_FROM_FVR_EIGHT_BIT/ADRESH);
    
    return mv;
    // TODO: Return without blocking for the end of the conversion
}

// Read Vcc in counts, trying for maximum noise
uint8_t ADC_read_vcc_fast(void)
{
    // Set ADC clock to Fosc (so that we get a noisy result and don't have to turn on another oscillator),
    // results right-justified (we want the noisy bits), and turn the module on
    ADCON0 = 0b10000100;
    
    // Set the measured channel to FVR and reference to Vdd
    ADPCH = 0b111111;
    ADREF = 0b00000000;
    
    // Set acquisition time to almost nothing
    ADACQ = 1;
    
    // Turn on FVR at 1024 mV for the ADC and wait for it to stabilize
    FVRCON = 0b10000001;
    while (!FVRRDY);
    
    // Turn on ADC
    ADON = 1;
    
    // Start conversion
    ADGO = 1;
    
    // Wait for completion
    while (ADGO);
    
    // Turn off ADC
    ADON = 0;
    
    // Turn off FVR and buffer
    FVRCON = 0b00000000;
        
    return ADRESL;
}


uint8_t ADC_read_rf(void)
{
    // Set ADC clock to internal (FRC), results left-justified
    ADCON0 = 0b00010000;
    
    // Set the measured channel to RA0 (ANA0) and reference to Vdd
    ADPCH = 0b000000;
    ADREF = 0b00000000;
    
    // Set acquisition time to 10 ADC clocks (10 us)
    ADACQ = 10;
        
    // Turn on ADC
    ADON = 1;
    
    // Start conversion
    ADGO = 1;
    
    // Wait for completion
    while (ADGO);
    
    // Turn off ADC
    ADON = 0;
    
    return (uint8_t)(ADRESH);    
}

// Linear feedback shift register random number generator. Has a period of 127.
uint8_t ADC_random_int(void)
{    
    // Generate feedback by XORing bits at specific taps
    // This next line is equivalent to feedback = ((mRandomState >> 6) ^ (mRandomState >> 5)) & 1;
    // except that it reduces the number of shifts, which must be done one at a time
    // on this platform in loops (we save at least 24 machine cycles this way)
//    uint8_t feedback = ((mRandomState >> 6) ^ (mRandomState >> 5)) & 1;
    uint8_t feedback = !!(((mRandomState >> 1) ^ mRandomState) & 0b00100000);

    // Shift the register and insert feedback at the lowest bit
    mRandomState = (uint8_t)(mRandomState << 1) | feedback;

    return mRandomState;
}

void ADC_set_random_seed(uint8_t seed)
{
    // Ensure it's non-zero and not the degenerate case (seed == 128)
    if (seed == 0 || seed == 128)
    {
        mRandomSeed = 0x35;
    }
    else
    {
        mRandomSeed = seed;    
    }

    mRandomState = mRandomSeed;    
}

// Return the most recent random number
uint8_t ADC_get_random_state(void)
{
    return mRandomState;
}
