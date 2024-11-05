#include "common.h" // MUST come first due to pragmas
#include "global.h"

#include "adc.h"
#include "prefs.h"
#include "leds.h"
#include "rf.h"
#include "supercap.h"

#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

// Macros and constants

// For best performance, sample after a power of 2 ticks
#define SAMPLE_VCC_EVERY_TICKS      (32)

// Increments above which high-latency timers will be used
#define HIGH_LATENCY_TIMER_THRESH   (16 << 2) // multiplied by four due to the timer taking quarter-ms increments

// Make sampling of RF voltages more random
#define RF_SAMPLING_MASK    (0x0F)
#define WHITENING           (0x5A)

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

// During bootup, we manually set the clock to 16 MHz, so set the internal state
// accordingly
static clock_speed_t mSystemClock = CLK_FAST;

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
    
    // Digital *OUTPUT* driver connection (default = 1 = push-pull driver disconnected)
    TRISC = /* Push-pull outputs */ (uint8_t)~(KEEP_ON_PIN | LED_BACKDRIVE_PIN_1 | LED_BACKDRIVE_PIN_2 | DEBUG_PIN) |
            /* Inputs or Weak pull-up output*/ (uint8_t)(SUPERCAP_MONITOR_PIN | SUPERCAP_MED_CHRG_PIN | LED_STOKER_PIN);
    TRISB = 0b11000000; // All port B outputs except programming pins
    TRISA = 0b00000001; // All outputs except RA0
    
    // Analog vs Digital *INPUT* selection (Important: affects only the input! Can still drive analog pins digitally if TRISCn=0)
    // We don't use any digital inputs in this project, so leave them all as analog
    ANSELC = 0xFF;
    ANSELB = 0xFF; // All port B digital except programming pins
    ANSELA = 0xFF; // All digital except RA0
    
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
    TMR0L = 0;
    
    // Timer6 -- Programmable callback
    T6CLKCON = 0x04; // LFINTOSC (31 kHz))
    T6CONbits.CKPS = 0b011; // 1:8 prescaler, gives roughly 4 kHz rate
    
    
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
    
    // Enable watchdog timer. Set to a 2-second timeout in the config bits
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
// Does no bounds checking. Increments are quarter milliseconds (i.e., to 
// have a 1 ms timeout, pass a value of 4), though note that there is about 100 us of overhead
// If the delay is longer than 16 ms, the timeout will be serviced with the
// sysclock set to the LTFINTOSC, so latency will be high (but power consumption
// will be low)
void TIMER_once(func_t pCallback, uint8_t quarterMilliseconds)
{
    // Start a new timer only if we don't already have one pending
    if (!mpTimerExpireCallback && 
        quarterMilliseconds > 0)
    {
        TMR6 = 0;
        mpTimerExpireCallback = pCallback;
        
        // The timer match effectively adds one, so compensate for that
        T6PR = quarterMilliseconds - 1;
        
        TMR6IF = 0;
        TMR6IE = 1;
        TMR6ON = true;
    }
}


void system_tick_handler(void)
{
    static bool sChargingCap = false;
    static uint8_t sRfLevel = 0;
    bool twinkled = false;
    
    // Avoid almost all of the slower work if we've just started up, as we might
    // be in an extremely compromised power state
    if (gTickCount > (1*TICKS_PER_SEC))
    {
        // Measure VDD with the ADC using the FVR about once every other second or on every tick 
        // if we're charging the supercap (so as to avoid brownout), but not just after startup
        if (gTickCount % SAMPLE_VCC_EVERY_TICKS == 0 ||
            sChargingCap)
        {
            gVcc = ADC_read_vcc();
        }
        
        // Whiten the "random" number because the LFSR gives long runs of similar lower bits.
        // This significantly improves the uniformity of the distribution of RF level sampling
        uint8_t moduloMatch = (RF_SAMPLING_MASK & (WHITENING ^ ADC_get_random_state()));

        // Measure the RF level with the ADC about once per second and add it to the running average to set the slicer
        // and detect whether there's any RF available to possibly decode.
        // If the match is "< 4" out of 16, that's a hit on a given tick of 25% (i.e., with 50ms)
        // that is roughly a 50% chance of sampling within 150 ms,
        // a 75% chance of sampling within 250 ms,
        // a 90% chance of sampling within 400 ms,
        // and a 99% chance of sampling within 800 ms
        // Given that the sampling history runs 8 deep, the history will cover about 1.2 seconds on average but between 0.4 s and 6 seconds 99% of the time
        if (moduloMatch < 0x04) 
        {
            sRfLevel = RF_update_slicer_level();
        }
        
        RF_sample_bit();
    
        // Charge the supercap if we're feeling spicy
        sChargingCap = SUPERCAP_charge();
    }
    
    // If we're not twinkling or using the fast callback timer for some other reason (like ACKing RF commands) show the RF status
    if ((gTickCount & 1))
    {
        if (!mpTimerExpireCallback)
        {
            LED_show_power(sRfLevel);
        }
    }
    
    // Twinkle the LEDs, but only if we don't already have a status LED showing and only on every other tick (10 Hz)
    // Blink only every tick for normal power, skipping the rest of this.
    // NOTE: This is not an "else" to the RF blink!
    if ((gTickCount & 1) == 0 ||
            (gPrefsCache.fastBlinksEn && gVcc > LED_BLINK_LOW_THRESH_MV))
    {
        if (!mpTimerExpireCallback)
        {
            LED_twinkle();
        }
    }    
    
    // Pet watchdog
    CLRWDT();
}


void main(void)
{
    // Turn on the regulator AS SOON AS POSSIBLE, to the exclusion
    // of every other priority
    PORTC = KEEP_ON_PIN; // Pull C0 high as soon as possible
    TRISC = (uint8_t)~KEEP_ON_PIN; // Connect as a digital output

    // Speed up HFINTOSC to 16 MHz as soon as we're done taking care of regulators. Do it
    // this way rather than starting with a higher Fosc (in the config bits) so that we
    // minimize current consumption while the KEEP_ON_PIN is not yet asserted
    OSCFRQ = 0b101; // 16 MHz HFINTOSC
    OSCCON1 = 0b110 << 4 | 0b0000; // HFINTOSC, divisor 1, 16 MHz net

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

    // Seed the random number generator with entropy. Do only one call, since 
    ADC_set_random_seed(ADC_read_vcc_fast());

    // Service the system tick immediately
    mUnhandledSystemTick = true;
    
    // Enable systick
    T0EN = 1;
        
    // Loop forever
    while(true)
    {
        if (mUnhandledSystemTick)
        {   
            DEBUG_SET();
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
            DEBUG_CLEAR();

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
