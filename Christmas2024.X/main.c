#include "common.h" // MUST come first due to pragmas
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

// Macros and constants

// Typedefs 

// Private variables
// Goes true when Timer 0 has expired
static bool mUnhandledSystemTick = false;

// Whether the fast (non-LF) system clock is currently being used
static bool mSystemFastClock = false;

static uint8_t mLedCounter = 0;

// Function implementations

// Setup of the pins and peripherals
void setup(void)
{   
    //
    // GPIO
    //
    
    // Initialization states
    PORTB = 0;
    
    // Input/output states (default = input)
    TRISBbits.TRISB0 = 0; // RB0 = output
    
    // Analog/digital
    ANSELBbits.ANSB0 = 0; // RB0 = digital
    
    // Slew rate
    SLRCONB = 0xFF; // limit all PORTB
    
    //
    // Timers
    //
    
    // Timer0 -- System tick
    T0CON1bits.T0CS = 0b0100; // clock source = LFINTOSC (31 kHz))
    T0CON1bits.T0CKPS = 0b1001; // prescaler = 512 (for 60.5 Hz base rate)
    T0CON0bits.T0OUTPS = 0; // postscaler = 1
    T0EN = 0; // Timer off for now
    TMR0IF = 0; // Clear interrupt flag
    TMR0IE = 1; // Enable interrupts
    TMR0L = 0; // Initialize timer count to zero
    TMR0H = 1; // interrupt every other tick, roughly every 32 ms
    
    
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
    
    // Enable systick
    T0EN = 1;
}

// Allow the system clock to be switched back and forth between 15.5 kHz and 16 MHz
// Handles all of the timer clock divider changes so that items based on Fosc
// continue operating normally.
void switchSystemClock(bool fast)
{
    // Early return if there is no change
    if (fast == mSystemFastClock)
    {
        return;
    }
        
    if (fast)
    {
        // Splitting the mSystemFastClock assignment like this saves two instruction 
        // cycles versus the more obvious method of assigning it the value of "fast"
        // before the conditional block
        mSystemFastClock = true;
    
        // Switch to fast clock
        // TODO: Was earlier testing done with HFINTOSC still divided by 2?
        OSCCON1 = 0b110 << 4 | 0b0000; // HFINTOSC, which we configured to be 16 MHz in setup()
        while (!OSCCON3bits.ORDY);
    }
    else
    {
        // Splitting the mSystemFastClock assignment like this saves two instruction 
        // cycles versus the more obvious method of assigning it the value of "fast"
        // before the conditional block
        mSystemFastClock = false;
    
        // Switch to slow clock, which is always ready
        OSCCON1 = 0b101 << 4 | 0b0001; // LFINTOSC, divide by 2. Net = 15.5 kHz
    }
}


// TODO: Add a (single) programmable timer with a callback callable after a specified 
// number of milliseconds, perhaps with a single uint8_t argument 


void system_tick_handler(void)
{
    // TODO
    
    // Testing: just blink an LED
    if (mLedCounter == 0)
    {
        // LED on
        PORTB = 0x01;
    }
    else
    {
        // LED off
        PORTB = 0x00;
    }
    
    mLedCounter++;
    mLedCounter &= 0x0F; // blink every time this wraps, about twice per second
    
    // Pet watchdog
    // (The watchdog is automatically pet when going to sleep, so maybe this is unneeded?)
    CLRWDT();
}

void main(void)
{
    di();
    setup();
    ei();
    
    // Loop forever
    while(true)
    {
        if (mUnhandledSystemTick)
        {   
            mUnhandledSystemTick = false;
            
            // Do the appropriate actions for the current state
            system_tick_handler();
            
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
 
    return;
}