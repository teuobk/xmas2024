#include "common.h" // MUST come first due to pragmas
#include "global.h"

#include "adc.h"
#include "prefs.h"
#include "leds.h"
#include "rf.h"

#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

// Macros and constants

// For best performance, sample after a power of 2 ticks
#define SAMPLE_VCC_EVERY_TICKS      (32)
#define SAMPLE_VRF_EVERY_TICKS      (16)

// Supercap charging action thresholds [mV]
#define SUPERCAP_CHRG_THRESH_SLOW      (2500)
#define SUPERCAP_CHRG_THRESH_MEDIUM    (2700)
#define SUPERCAP_CHRG_THRESH_FAST      (3000)
#define SUPERCAP_CHRG_THRESH_LIMITTER  (3450) // 3300 mV plus worst-case Schottky drop (at 10 uA on a hot day) of 200 mV, plus a little margin
#define SUPERCAP_CHRG_THRESH_PROTECT   (3600) // 3300 mV plus typical Schottky drop of 300 mV at 100 uA for these packages

// Increments above which high-latency timers will be used
#define HIGH_LATENCY_TIMER_THRESH   (32)

// Typedefs 

typedef enum
{
    CLK_SLOW,
    CLK_MED,
    CLK_FAST
} clock_speed_t;

// Non-macro constants

// Module variables
// Goes true when Timer 0 has expired
static bool mUnhandledSystemTick = false;

static clock_speed_t mSystemClock = CLK_SLOW;

static func_t mpTimerExpireCallback = NULL;

uint32_t gTickCount = 0; // absolute tick count

extern uint16_t gVcc;

// Global variables and pseudo-variables



// Function implementations

// Setup of the pins and peripherals
void setup(void)
{   
    //
    // GPIO
    //
    
    // Initialization states low for most everything
    PORTC = KEEP_ON_PIN;
    PORTB = 0;
    PORTA = 0;
    
    // Digital output driver connection (default = 1 = push-pull driver disconnected)
    TRISC = /* Push-pull outputs */ (uint8_t)~(KEEP_ON_PIN | LED_BACKDRIVE_PIN_1 | LED_BACKDRIVE_PIN_2 | DEBUG_PIN) |
            /* Inputs or Weak pull-up output*/ (uint8_t)(SUPERCAP_CHRG_PIN | SUPERCAP_MED_CHRG_PIN | LED_STOKER_PIN);
    TRISB = 0b11000000; // All port B outputs except programming pins
    TRISA = 0b00000001; // All outputs except RA0
    
    // Analog vs Digital input selection (Important: affects only the input! Can still drive analog pins digitally if TRISCn=0)
    ANSELC = (uint8_t)~(KEEP_ON_PIN | DEBUG_PIN); 
    ANSELB = 0b11000000; // All port B digital except programming pins
    ANSELA = 0b00000001; // All digital except RA0
    
    // Slew rate
    SLRCONC = 0xFF; // limit all PORTC
    SLRCONB = 0xFF; // limit all PORTB
    SLRCONA = 0xFF; // limit all PORTA
    
    //
    // Timers
    //
    
    // Timer0 -- System tick
    T0CON1bits.T0CS = 0b0100; // clock source = LFINTOSC (31 kHz))
    T0CON1bits.T0CKPS = 0b0101; // prescaler = 32 (for 968 Hz base rate)
    T0CON0bits.T0OUTPS = 0; // postscaler = 1
    T0EN = 0; // Timer off for now
    TMR0IF = 0; // Clear interrupt flag
    TMR0IE = 1; // Enable interrupts
    TMR0H = 48; // (tick count is this number + 1) interrupt every 50 ms
    
    // Initialize systick timer count to almost expired. We do this to force a system
    // tick just after startup because forcing a system tick by setting the 
    // unhandled-tick flag "true" directly leaves some sort of high-power thing 
    // running until the first systick interrupt
    TMR0L = TMR0H - 1; 
    
    // Timer6 -- Programmable callback
    T6CLKCON = 0x04; // LFINTOSC (31 kHz))
    T6CONbits.CKPS = 0b100; // 1:16 prescaler, gives roughly 2 kHz rate
    
    
    //
    // Power and interrupts
    //
    
    // Disable clocking to modules we're not using
    PMD0 = 0b00011011; // Disable CRC module, program memory scanner, clock reference, GPIO interrupt-on-change
    PMD1 = 0b10111110; // Disable all timers except TMR6 and TMR0
    PMD2 = 0b00000001; // Disable zero-crossing detector
    PMD3 = 0b11111111; // Disable all CCP modules and PWM modules
    PMD4 = 0b11111111; // Disable all UARTs, serial modules, and complementary waveform generators
    PMD5 = 0b11111111; // Disable all signal measurement timers, CLCs, and DSMs
    
    // Enable idle mode
    CPUDOZEbits.IDLEN = 1;
    
    // General peripheral interrupt enable
    PEIE = 1;
    
    // Enable watchdog timer
    WDTCON0bits.SWDTEN = 1;
}

// Allow the system clock to be switched among 15.5 kHz, 1 MHz, and 16 MHz
// Handles all of the timer clock divider changes so that items based on Fosc
// continue operating normally.
void switchSystemClock(bool fast)
{
    // Early return if there is no change
    if (fast && 
        CLK_FAST == mSystemClock)
    {
        return;
    }
    
    if (!fast &&
        CLK_SLOW == mSystemClock)
    {
        return;
    }
        
    if (fast)
    {
        // Switch to fast clock
        // Don't bother letting the clock stabilize, because it doesn't matter
        // for this application
        OSCFRQ = 0b101; // 16 MHz HFINTOSC
        OSCCON1 = 0b110 << 4 | 0b0000; // HFINTOSC, divisor 1, 16 MHz net
        
        mSystemClock = CLK_FAST;
    }
    else
    {
        // Keep clock running faster if we have a pending high-resolution timer expiration, unless
        // that high-res timer's callback is particularly long way into the future
        if (mpTimerExpireCallback && 
            T6PR < HIGH_LATENCY_TIMER_THRESH)
        {
            mSystemClock = CLK_MED;
            
            // 1 MHz system clock 
            OSCFRQ = 0b000; // 1 MHz HFINTOSC
            
            // we should already be running with these settings (notably a divisor of 1) at this point, 
            // assuming we never go from 15 kHz -> 1 MHz, so no need to write them again
//            OSCCON1 = 0b110 << 4 | 0b0000; // HFINTOSC, which we configured to be 16 MHz in setup()
        }
        else
        {
            mSystemClock = CLK_SLOW;

            // Switch to the LFINTOSC, which is always ready, and divide by 2 for extra power savings
            OSCCON1 = 0b101 << 4 | 0b0001; // LFINTOSC, 31 kHz divide by 2. Net = 15.5 kHz
        }
    }
}

// Set up a timer to call the callback in the specified time
// Does no bounds checking. Increments are half milliseconds (i.e., to 
// have a 1 ms timeout, pass a value of 2)
// If the delay is longer than 16 ms, the timeout will be serviced with the
// sysclock set to the LTFINTOSC, so latency will be high (but power consumption
// will be low)
void TIMER_once(func_t pCallback, uint8_t halfMilliseconds)
{
    // Start a new timer only if we don't already have one pending
    if (!mpTimerExpireCallback && 
        halfMilliseconds > 0)
    {
        TMR6 = 0;
        mpTimerExpireCallback = pCallback;
        
        // The timer match effectively adds one, so compensate for that
        T6PR = halfMilliseconds - 1;
        
        TMR6IF = 0;
        TMR6IE = 1;
        TMR6ON = true;
    }
}

// Determine whether we should charge the supercap, and at what rate
// Returns true if charging the supercap, false otherwise
bool supercap_charge(void)
{
    bool chargingCap = false;
    
    // Note how we're controlling only the TRIS connections for the relevant pins,
    // as ANSEL affects only the input buffer (and thus we want to leave them 
    // on "analog" especially if the net levels due to loading are in between digital ones)
    
    // Charge the supercap?
    if (gVcc > SUPERCAP_CHRG_THRESH_PROTECT ||
        !gPrefsCache.supercapChrgEn)
    {
        // Voltage is too high to safely charge the cap, or charging is disabled
        LATC = (LATC & ~(SUPERCAP_MED_CHRG_PIN | SUPERCAP_CHRG_PIN));
        TRISCbits.TRISC5 = 1;
        TRISCbits.TRISC7 = 1;
        WPUC5 = 0;
    }
    else if (gVcc > SUPERCAP_CHRG_THRESH_LIMITTER)
    {
        // Go back to slow charging as we're approaching the limit, and we want
        // to take advantage of the diode drop and the current limit for
        // the weak pull-up (which will be pretty low anyway at this point))
        TRISCbits.TRISC5 = 1;
        TRISCbits.TRISC7 = 1;
        LATC = (LATC & ~(SUPERCAP_MED_CHRG_PIN | SUPERCAP_CHRG_PIN));
        WPUC5 = 1;
        chargingCap = true;
    }
    else if (gVcc > SUPERCAP_CHRG_THRESH_FAST)
    {
        // Fast-charge cap (GPIO)
        LATC = (LATC & ~(SUPERCAP_MED_CHRG_PIN | SUPERCAP_CHRG_PIN)) | SUPERCAP_CHRG_PIN; // RC5 = 1; // use LATC in case the starting level is low
        TRISCbits.TRISC5 = 0;
        TRISCbits.TRISC7 = 1;
        WPUC5 = 0;
        chargingCap = true;
    }
    else if (gVcc > SUPERCAP_CHRG_THRESH_MEDIUM)
    {
        // Medium-charge cap (GPIO) (through the 3.3k resistor)
        LATC = (LATC & ~(SUPERCAP_MED_CHRG_PIN | SUPERCAP_CHRG_PIN)) | SUPERCAP_MED_CHRG_PIN; // RC7 = 1; // use LATC in case the starting level is low
        TRISCbits.TRISC5 = 1;
        TRISCbits.TRISC7 = 0;
        WPUC5 = 0;
        chargingCap = true;
    }
    else if (gVcc > SUPERCAP_CHRG_THRESH_SLOW)
    {
        // Slow-charge cap (weak pull-up)
        LATC = (LATC & ~(SUPERCAP_MED_CHRG_PIN | SUPERCAP_CHRG_PIN));
        TRISCbits.TRISC5 = 1;
        TRISCbits.TRISC7 = 1;
        WPUC5 = 1;
        chargingCap = true;
    }
    else
    {
        // Stop charging cap (if we're doing so)
        LATC = (LATC & ~(SUPERCAP_MED_CHRG_PIN | SUPERCAP_CHRG_PIN));
        TRISCbits.TRISC5 = 1;
        TRISCbits.TRISC7 = 1;
        WPUC5 = 0;
    }   
        
    return chargingCap;
}

void system_tick_handler(void)
{
    static bool sChargingCap = false;
        
    // Measure VDD with the ADC using the FVR about once every other second or on every tick 
    // if we're charging the supercap (so as to avoid brownout), but not just after startup
    if ( (gTickCount > (1*TICKS_PER_SEC) && (gTickCount % SAMPLE_VCC_EVERY_TICKS) == 0) ||
        sChargingCap)
    {
        gVcc = ADC_read_vcc();
    }
    
    // Make sampling of RF voltages more random
    uint8_t moduloMatch = (0x0F & ADC_get_random_state());

    // Measure the RF level with the ADC about once per second and add it to the running average to set the slicer
    // and detect whether there's any RF available to harvest, but not just after startup
    if (gTickCount > (1*TICKS_PER_SEC) && 
        (gTickCount % SAMPLE_VRF_EVERY_TICKS) == moduloMatch)
    {
        RF_update_slicer_level();
    }
        
    RF_sample_bit();
    
    sChargingCap = supercap_charge();

    // Twinkle the LEDs, but only if we don't already have a status LED showing and only on every other tick (10 Hz)
    if (!mpTimerExpireCallback &&
            ((gTickCount & 1) == 0))
    {
        LED_twinkle();
    }
    
    // Pet watchdog
    CLRWDT();
}


void main(void)
{
    // Turn on the regulator AS SOON AS POSSIBLE, to the exclusion
    // of every other priority
    PORTC = KEEP_ON_PIN; // Pull C0 high as soon as possible
    TRISC = (uint8_t)~KEEP_ON_PIN;
    ANSELC = (uint8_t)~KEEP_ON_PIN;

    // Speed up HFINTOSC to 16 MHz as soon as we're done taking care of regulators. Do it
    // this way rather than starting with a higher Fosc (in the config bits) so that we
    // minimize current consumption while the KEEP_ON_PIN is not yet asserted
    OSCFRQbits.HFFRQ = 0b101;
    
    setup();
    ei();
    
#ifdef EXPOSE_FOSC_ON_PIN    
    // DEBUGGING: Export Fosc to TP9 to check for e.g., what speed the MCU is actually running at
    // NOTE: Be sure to re-enable the power to the CLKR in PMD0 !
    CLKRCLK = 0x00; // Fosc
    TRISBbits.TRISB5 = 0; // output
    ANSELBbits.ANSB5 = 0; // digital
    RB5PPS = 0x1A; // Export to pin RB5 (pin 26))
    CLKRCONbits.CLKREN = 1; // enable clock output
#endif
    
    // Cache load
    PREFS_init();
    
    // Seed the random number generator with entropy
    ADC_set_random_state(ADC_gen_entropy() ^ ADC_read_vcc_fast());
    
    // Enable systick
    T0EN = 1;
            
    // Loop forever
    while(true)
    {
        if (mUnhandledSystemTick)
        {   
            mUnhandledSystemTick = false;
            
            // Enable BOR detection temporarily. Should respond within 2 us
            // per datasheet if Vcc < 1.9 V, and GPIO should float within 
            // another 2 us. At 16 MHz Fosc, that's 8 instructions, fewer if
            // branches are involved
            BORCON = 0x80; // Enable BOR detection temporarily
            
            // Do the appropriate actions for the current state
            system_tick_handler();
            gTickCount++;
            
            // Disable BOR detection to save power (consumes 9 uA when active)
            BORCON = 0x00; // Enable BOR detection temporarily
            
            switchSystemClock(false);
        }

        // Wait for next interrupt
        SLEEP();
    }
}

// Interrupt handler. 
// On the PIC16, there is only one interrupt vector, so we need to search
// through the interrupt flags to figure out which one we got
void __interrupt() isr(void)
{    
    // Timer 0 -- System tick timer
    if (TMR0IE && TMR0IF)
    {
        // PROMPTLY speed up the system clock, then clean up with a normal call
        OSCFRQ = 0b101; // 16 MHz HFINTOSC
        OSCCON1 = 0b110 << 4 | 0b0000; // HFINTOSC, divisor 1, 16 MHz net
        switchSystemClock(true);
        
        mUnhandledSystemTick = true;

        TMR0IF = 0;
        // Timer auto-reloads
    }
    
    // Timer 6 -- Programmable timer callback
    if (TMR6IE && TMR6IF)
    {
        TMR6IE = 0;
        
        if (mpTimerExpireCallback)
        {
            mpTimerExpireCallback();
            mpTimerExpireCallback = NULL;        
            TMR6ON = false;
            switchSystemClock(false);
        }
    }
 
    return;
}
