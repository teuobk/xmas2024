#include "common.h" // MUST come first due to pragmas
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

// Macros and constants

// Typedefs 

typedef enum
{
    CLK_SLOW,
    CLK_MED,
    CLK_FAST
} clock_speed_t;

typedef void (*func_t)(void);

// Private variables
// Goes true when Timer 0 has expired
static bool mUnhandledSystemTick = false;

static clock_speed_t mSystemClock = CLK_SLOW;

static uint8_t mLedCounter = 0;

static func_t mpTimerExpireCallback = NULL;

// Function implementations

// Setup of the pins and peripherals
void setup(void)
{   
    //
    // GPIO
    //
    
    // Initialization states
    PORTB = 0;
    PORTA = 0;
    
    // Input/output states (default = input)
    TRISB = 0b11100100; // RB0, RB1, RB3, RB4 outputs
    TRISA = 0b11011111; // RA5 output
    
    // Analog/digital
    ANSELB = 0b11100100; // RB0, RB1, RB3, RB4 digital
    ANSELA = 0b11011111; // RA5 digital
    
    // Slew rate
    SLRCONB = 0xFF; // limit all PORTB
    
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
    TMR0H = 96; // (tick count is this number + 1) interrupt every 100 ms
    TMR0L = TMR0H - 1; // Initialize timer count to almost expired
    
    // Timer6 -- Programmable callback
    T6CLKCON = 0x04; // LFINTOSC (31 kHz))
    T6CONbits.CKPS = 0b100; // 1:16 prescaler, gives roughly 2 kHz rate
    
    // Ensure HFINTOSC is at 16 MHz
    OSCFRQbits.HFFRQ = 0b101;
    
    // Ensure FOSC is LFINTOSC/2
    OSCCON1bits.NOSC = 0b101; // LFINTOSC, 31 kHz
    OSCCON1bits.NDIV = 0b0001; // Divide by 2 = net 15.5 kHz system osc
    
    // Enable idle mode
    CPUDOZEbits.IDLEN = 1;
    
    // General peripheral interrupt enable
    PEIE = 1;
    
    // Enable watchdog timer
    WDTCON0bits.SWDTEN = 1;
    

}

// Allow the system clock to be switched back and forth between 15.5 kHz and 16 MHz
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
        // Splitting the mSystemFastClock assignment like this saves two instruction 
        // cycles versus the more obvious method of assigning it the value of "fast"
        // before the conditional block
        mSystemClock = CLK_FAST;
    
        // Switch to fast clock
        // TODO: Was earlier testing done with HFINTOSC still divided by 2?
        OSCFRQ = 0b101; // 16 MHz HFINTOSC
        OSCCON1 = 0b110 << 4 | 0b0000; // HFINTOSC, divisor 1, 16 MHz net

        // TODO: Does letting the clock stabilize really matter in our use case?
//        while (!OSCCON3bits.ORDY);
    }
    else
    {
        // Keep clock running faster if we have a pending high-resolution timer expiration
        if (mpTimerExpireCallback)
        {
            mSystemClock = CLK_MED;
            
            // 1 MHz system clock 
            OSCFRQ = 0b000; // 1 MHz HFINTOSC
            
            // We should already be running with these settings at this point, so no need to write them again
//            OSCCON1 = 0b110 << 4 | 0b0000; // HFINTOSC, which we configured to be 16 MHz in setup()
        }
        else
        {
            // Splitting the mSystemFastClock assignment like this saves two instruction 
            // cycles versus the more obvious method of assigning it the value of "fast"
            // before the conditional block
            mSystemClock = CLK_SLOW;

            // Switch to slow clock, which is always ready
            OSCCON1 = 0b101 << 4 | 0b0001; // LFINTOSC, divide by 2. Net = 15.5 kHz

        }
    }
}

// Turn off all LEDs. INTERRUPT CALLBACK USE ONLY.
static void turnOffAllLeds(void)
{
    PORTB = 0;
}

static void backdrivePulseOver(void)
{
    PORTA = 0;
}

// Set up a timer to call the callback in the specified time
// Does no bounds checking. 
static void timer_once(func_t pCallback, uint8_t milliseconds)
{
    if (!mpTimerExpireCallback)
    {
        TMR6 = 0;
        mpTimerExpireCallback = pCallback;
        
        // The timer match effectively adds one, so compensate for that
        T6PR = (milliseconds * 2) - 1;
        
        TMR6IF = 0;
        TMR6IE = 1;
        TMR6ON = true;
    }
}

// TODO: Add a (single) programmable timer with a callback callable after a specified 
// number of milliseconds, perhaps with a single uint8_t argument 


void system_tick_handler(void)
{
#define INTERVAL_DEFAULT 0b00010000
    static uint8_t sIntervalControl = INTERVAL_DEFAULT;
    
    // TODO
    
    // Testing: just blink an LED
    if (mLedCounter % sIntervalControl == 0)
    {
        // LED on
        PORTB = 1 << 3;
        
        timer_once(turnOffAllLeds, 1);
        
        if (sIntervalControl > 0b00001000)   
        {
            sIntervalControl >>= 1;
        }
        else
        {
            sIntervalControl <<= 1;
        }
               
    }
    else if (mLedCounter % sIntervalControl == 1)
    {
        PORTB = 1 << 1;
        
        timer_once(turnOffAllLeds, 1);
    }
    else if (mLedCounter % sIntervalControl == 2)
    {
        PORTB = 1 << 4;
        
        timer_once(turnOffAllLeds, 1);
    }
    else if (mLedCounter % sIntervalControl == 3)
    {
        // Other LED on
        PORTB = 1 << 0;
        
        timer_once(turnOffAllLeds, 1);
    }
    else if (mLedCounter % sIntervalControl == 4)
    {
        // Backdrive the collection LEDs
        PORTA = 1 << 5;
        timer_once(backdrivePulseOver, 1);
    }
    
    mLedCounter++; // will automatically wrap after 255 ticks
    
        
    // Pet watchdog
    // (The watchdog is automatically pet when going to sleep, so maybe this is unneeded?)
    CLRWDT();
}

void main(void)
{
    // Turn on the regulator AS SOON AS POSSIBLE, to the exclusion
    // of every other priority
    PORTC = 0b00000001; // Pull C0 high as soon as possible
    TRISC = 0b11111110;
    ANSELC = 0b11111110;

    setup();
    ei();
    
#ifdef EXPOSE_FOSC_ON_PIN    
    // DEBUGGING: Export Fosc to TP9
    CLKRCLK = 0x00; // Fosc
    TRISBbits.TRISB5 = 0; // output
    ANSELBbits.ANSB5 = 0; // digital
    RB5PPS = 0x1A; // Export to pin RB5 (pin 26))
    CLKRCONbits.CLKREN = 1; // enable clock output
#endif
    
    // Tick once right at startup. Note: this is done this way
    // because setting the unhandled tick flag directly leaves
    // some sort of high-power thing running
    
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
        // Speed up system clock as soon as possible
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
            switchSystemClock(false);
        }
    }
 
    return;
}