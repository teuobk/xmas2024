#include "adc.h"
#include "global.h"


// Macros and constants

// Typedefs

// Variables

static uint8_t mRandomState; // Most recent PRNG state
static uint8_t mRandomSeed; // Most recently set seed

uint16_t gVcc = 0;

// Implementations


// Read Vcc in tens of mV
// Takes about 140 us when Fosc=16MHz, almost entirely due to the division for conversion to millivolts
uint16_t ADC_read_vcc(void)
{
    uint16_t mv = 0;
    
    // Set ADC clock to internal (FRC), results left-justified (the ADC has only about 8 bits ENOB anyway)
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
    
    // Convert the value into millivolts. Note that this isn't exactly 
    // optimal; however, it's done this way to avoid a lengthy 32-bit
    // divide, instead reducing the problem to a 16-bit divide followed by two left-rotates
    // This nets roughly 1024*255/ADRESH in something like 60% fewer cycles 
    // (note that the value 1024 comes from the FVR reference voltage in millivolts)
    mv = (uint16_t)(65535u/ADRESH*4u); // TODO: should be 65280
    
    return mv;
}

// Read the supercap voltage relative to Vdd to ensure that it doesn't go above
// 3.3 V. This takes advantage of several things: first, the supercap has relatively
// high leakage when charged above 3.0 V (tens of microamps); second, the supercap
// has an ESR of about 100 ohms; third, the drop across the diode for charging the
// supercap is between about 100 mV (at If=100nA) and about 370 mV (at If=1mA).
// If we assume that leakage across the cap at high charge voltages will keep the 
// charge current above 1uA, then Vf will always be 173 mV or larger.
// 
// So, we use this measurement for two things. First, we check to make sure we
// always have 1 uA or more of current going to the supercap when Vdd is above 3300 mV.
// Since it's a 3300 ohm shunt resistor, that's 3.3 mV. That's about 1 ADC count
// when Vdd is 3300 mV and we're in 10-bit mode (3300 / 1024 ~= 3.3) -- except
// that's into the ADC noise. The minimum realistic change we can detect is about
// 3300 / 256 ~= 13 mV => 4 uA charge current, so let's look for that instead.
// (Possible complication: the current consumed by the ADC itself, which is likely to
// be more than 4 uA when the input is connected)
//
// Thinking about this more: just assume the drop across the diode to the supercap
// is going to be at least 200 mV, so the value here needs to be equivalent to 
// no more than 3300 + 200 = 3500 mV.
// 
// Thus: if Vdd <= 3500 mV, there's never a problem
// 
// To keep it simple, look for the delta in counts to be at least (Vdd-3500)/16
// So, for example, if Vdd = 3550 mV, (3550-3500)/16 = 3 counts. As long as the delta
// is greater than the one calculated in this manner, the supercap won't see more
// than 3300 mV.
uint8_t ADC_read_supercap_relative(void)
{
    uint8_t countsDownFromVdd = 0;
    
    // Set ADC clock to internal (FRC), results left-justified 
    ADCON0 = 0b00010000;
    
    // Set the measured channel to ANC5 and reference to Vdd
    ADPCH = 0b010101;
    ADREF = 0b00000000;
    
    // Set acquisition time to 30 ADC clocks (30 us) (based on the net 13 kOhm impedance)
    ADACQ = 30;
        
    // Turn on ADC
    ADON = 1;
    
    // Start conversion
    ADGO = 1;
    
    // Wait for completion
    while (ADGO);
    
    // Turn off ADC
    ADON = 0;
    
    countsDownFromVdd = UINT8_MAX - ADRESH;
    
    return countsDownFromVdd;
}


// Read Vcc in counts, trying for maximum noise
// Takes about 9 us when Fosc=16MHz
uint8_t ADC_read_vcc_fast(void)
{
    // Frc as clock source. Earlier, Fosc/2 had been used as the ADC clock source,
    // but when Vcc is at about 2.50 V +/- 0.05 V, then sometimes (not always, but
    // often) the ADC conversion will fail to complete, and the system will hang
    // waiting for ADGO to go low. It's really strange. At 2.6 V or above, everything
    // is rock-solid, and at 2.4 V or below, everything is also rock-solid, but
    // there's something weird about being at 2.5 V. This one took A VERY LONG TIME
    // to figure out, not least because this function is called only when entropy
    // is needed for the PRNG, which happens only every 12 seconds or so of
    // continuous uptime. I had seen some odd behavior while running on a very charged
    // supercap, in which the system would suddenly die and then reboot about 2 seconds
    // later, but it took putting the system on a bench supply and watching the KEEP_ON
    // line to realize that it was actually hanging and getting reset by the watchdog.
    // Changing to the Frc clock source fixes the issue. 
    ADCON0 = 0b10010100; 
    
    // Set the measured channel to FVR and reference to Vdd
    ADPCH = 0b111111;
    ADREF = 0b00000000;
    
    // Set acquisition time to almost nothing
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
            
    return ADRESL ^ ADRESH;
}

// Read the RF level for setting the comms slicer
// Takes about 120 us
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
