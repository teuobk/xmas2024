#include "leds.h"
#include "global.h"
#include "adc.h"
#include "prefs.h"

// Macros and constants

#define LED_CYCLE_LENGTH 0b00010000

#define RF_ACK_LED_PIN          (3)
#define RF_LVL_LED_PIN          (1)
#define HARVEST_STOKE_PIN       (3)

#define RF_ACK_BLINK_DURATION   (7) 
#define RF_LVL_BLINK_DURATION   (3)

#define LED_BLINK_TIME_LIMIT_HARSH_SITUATIONS   7 // MUST per a power of 2 - 1
#define LED_BLINK_LOW_THRESH_MV                 2400 // When the voltage is below this level, the situation is considered "low power" so the low time limit applies no matter the power mode

#define LED_HARVEST_STOKER_THRESH_MV            2300 // Should be above the voltage at which the system will be powerd on LEDs alone
#define LED_HARVEST_STOKER_TIME_MS              (18)

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
    
    // Variable length blink times, also ensuring blinkTime is non-zero
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
                    WPUC3 = 1;
                    TIMER_once(turnOffHarvestStoker, LED_HARVEST_STOKER_TIME_MS << 2);
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
    if (mLedCounter == UINT8_MAX)
    {
        // Re-seed when we wrap. Keep it fresh! 
        ADC_set_random_seed(ADC_get_random_state() ^ ADC_read_vcc_fast());
    }
}

// Show the RF level using what had been the NACK LED
void LED_show_power(uint8_t powerLevel)
{
    static uint8_t sCallCount = 0;
#define NUM_POWER_LEVELS  (4)

    // Don't bother doing extra calculations if we're not going to show anything anyway
    if (powerLevel > ((UINT8_MAX + 1) / NUM_POWER_LEVELS))
    {
        uint8_t powerLevelScaled = powerLevel / ((UINT8_MAX+1) / NUM_POWER_LEVELS);
        if (sCallCount < powerLevelScaled)
        {    
            PORTA = (uint8_t)(1 << RF_LVL_LED_PIN);
            TIMER_once(turnOffAllPortALeds, RF_LVL_BLINK_DURATION);
        }
    }
    
    sCallCount = (sCallCount + 1) % (NUM_POWER_LEVELS * 4);
}

// Blink the RF command ACK LED
void LED_blink_ack(void)
{
    PORTA = (uint8_t)(1 << RF_ACK_LED_PIN);
    TIMER_once(turnOffAllPortALeds, RF_ACK_BLINK_DURATION);
}
