#include "leds.h"
#include "global.h"
#include "adc.h"
#include "prefs.h"

// Macros and constants

#define LED_CYCLE_LENGTH 0b00010000
#define RF_ACK_LED_PIN          (3)
#define RF_NACK_LED_PIN         (1)
#define RF_ACK_BLINK_DURATION   (5)
#define HARVEST_STOKE_PIN       (3)

#define LED_BLINK_TIME_LIMIT_HARSH_SITUATIONS   0x03
#define LED_BLINK_LOW_THRESH_MV                 2400 // When the voltage is below this level, the situation is considered "low power" so the low time limit applies no matter the power mode

#define LED_HARVEST_STOKER_THRESH_MV            2350 // Should be above the voltage at which the system will be powerd on LEDs alone
#define LED_HARVEST_STOKER_TIME_MS              (20)

#define PORT_C_NON_LED_MASK  (0xFC)

// Typedefs

typedef enum
{
    LED_PORT_A,
    LED_PORT_B,
    LED_PORT_C,
    LED_IDLE,
} led_port_t;

typedef struct
{
    led_port_t  port;
    uint8_t     pin;
} led_blink_prog_step_t;

static const led_blink_prog_step_t cLedTwinkle[LED_CYCLE_LENGTH] =
{
    {LED_PORT_B, 1},
    {LED_PORT_B, 4}, // tree star
    {LED_PORT_A, 2},
    {LED_PORT_A, 6},
    {LED_PORT_B, 0},
    {LED_PORT_A, 4},
    {LED_PORT_C, 0},
    {LED_PORT_B, 5},
    {LED_PORT_B, 3},
    {LED_PORT_A, 5},
    {LED_PORT_B, 4}, // tree star
    {LED_PORT_B, 2},
    {LED_PORT_A, 7},
    {LED_PORT_C, 1},
    {LED_PORT_C, 3}, // "stoke" the harvest LED rail
    {LED_PORT_B, 4}, // tree star
};

// Variables

static uint8_t mLedCounter = 0;

// Implementations


// Turn off all port B LEDs. INTERRUPT CALLBACK USE ONLY.
static void turnOffAllPortBLeds(void)
{
    PORTB = 0;
}

// Turn off all port B LEDs. INTERRUPT CALLBACK USE ONLY.
static void turnOffAllPortALeds(void)
{
    PORTA = 0;
}

// Turn off all port C LEDs. INTERRUPT CALLBACK USE ONLY.
static void turnOffAllPortCLeds(void)
{
    LATC = LATC & PORT_C_NON_LED_MASK; // don't spoil the non-LED pins on port C
}

// Turn off the "soft" harvest LED stoker
static void turnOffHarvestStoker(void)
{
    ANSELCbits.ANSC3 = 1;
    WPUC3 = 0;                
}


// Twinkle the tree LEDs
void LED_twinkle(void)
{   
    uint8_t randomInt = ADC_random_int();
    uint8_t remainder = ((randomInt + mLedCounter) % LED_CYCLE_LENGTH);
         
    // Limit power at startup no matter what the preferences say
    uint8_t timeLimit = gPrefsCache.blinkTimeLimit;
    timeLimit = (gTickCount < (2*TICKS_PER_SEC)) ? MIN(timeLimit, LED_BLINK_TIME_LIMIT_HARSH_SITUATIONS) : timeLimit;
    
    // Also limit power if VCC is low
    timeLimit = (gVcc < LED_BLINK_LOW_THRESH_MV) ? MIN(timeLimit, LED_BLINK_TIME_LIMIT_HARSH_SITUATIONS) : timeLimit;
    
    // Variable length blink times, also ensuring blinkTime is non-zero. Will
    // produce blinks between 500 us and 2 ms
    uint8_t blinkTime = (randomInt & timeLimit) + 1;
    
    led_blink_prog_step_t currentStep = cLedTwinkle[remainder];
        
    switch (currentStep.port)
    {
        case LED_PORT_A:
            PORTA = (uint8_t)(1 << currentStep.pin);
            TIMER_once(turnOffAllPortALeds, blinkTime);
            break;
        case LED_PORT_B:
            // Allow the tree star to be enabled or disabled
            if (currentStep.pin != TREE_STAR_PIN ||
                gPrefsCache.treeStarEn)
            {
                PORTB = (uint8_t)(1 << currentStep.pin);
                TIMER_once(turnOffAllPortBLeds, blinkTime);
            }
            break;
        case LED_PORT_C:
            // Allow the harvest LEDs to be enabled or disabled
            if (currentStep.pin == HARVEST_STOKE_PIN && gPrefsCache.harvestRailChargeEn)
            {
                if (gVcc >= LED_HARVEST_STOKER_THRESH_MV)
                {
                    // "Stoke" with a weak pullup
                    ANSELCbits.ANSC3 = 1;
                    WPUC3 = 1;
                    TIMER_once(turnOffHarvestStoker, LED_HARVEST_STOKER_TIME_MS << 1);
                }
            }
            else if (gPrefsCache.harvestBlinkEn)
            {
                // Don't spoil the non-LED pins on port C
                LATC = (LATC & PORT_C_NON_LED_MASK) | (uint8_t)(1 << currentStep.pin);
                TIMER_once(turnOffAllPortCLeds, blinkTime);
            }
            break;
        case LED_IDLE:
            // If enabled, use idle periods to backcharge the LED harvest rail
            if (gPrefsCache.harvestRailChargeEn)
            {
                // TODO
            }
            break;
        default:
            // nop
            break;            
    }
    
    mLedCounter++; // will automatically wrap after 255 ticks    
    // If we just wrapped, also perturb the random number generator state,
    // being sure to keep it non-zero
    if (mLedCounter == 0)
    {
        ADC_set_random_state(randomInt ^ (uint8_t)(gVcc & 0x00FF));
    }
}
// Blink the RF command ACK LED
void LED_blink_ack(void)
{
    PORTA = (uint8_t)(1 << RF_ACK_LED_PIN);
    TIMER_once(turnOffAllPortALeds, RF_ACK_BLINK_DURATION);
}

// Blink the RF command NACK LED
void LED_blink_nack(void)
{
    PORTA = (uint8_t)(1 << RF_NACK_LED_PIN);
    TIMER_once(turnOffAllPortALeds, RF_ACK_BLINK_DURATION);
}
