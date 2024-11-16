#include "supercap.h"
#include "global.h"
#include "adc.h"
#include "prefs.h"

// Macros and constants

// The minimum forward drop of the schottky diode during charging
// (This is actually the value at about 4 uA of charging current)
#define DIODE_DROP_MIN                              (200)

// Max charge of the supercap without damage
#define SUPERCAP_MAX_MV                             (3300)

// This constant converts the difference between Vcc and the supercap 
// damage threshold (including diode drop) in millivolts into a number
// of counts that can be directly compared to the 8-bit measurement of
// the supercap charge-monitor pin
#define MV_TO_COUNTS_FOR_RELATIVE_SUPERCAP          (16)

// Supercap charging action thresholds [mV]
#define SUPERCAP_CHRG_THRESH_OFF_TO_SLOW_MIN        (2700)

#define SUPERCAP_CHRG_THRESH_SLOW_TO_OFF_UNDER      (2500)
#define SUPERCAP_CHRG_THRESH_SLOW_TO_OFF_OVER       (SUPERCAP_MAX_MV + DIODE_DROP_MIN)
#define SUPERCAP_CHRG_THRESH_SLOW_TO_FAST           (2950)


#define SUPERCAP_CHRG_THRESH_FAST_TO_OFF_OVER       (SUPERCAP_MAX_MV + DIODE_DROP_MIN)
#define SUPERCAP_CHRG_THRESH_FAST_TO_OFF_UNDER      (2500)
#define SUPERCAP_CHRG_THRESH_FAST_TO_SLOW           (2700)

#define TICKS_BOOTUP_TO_OFF                         (2 * TICKS_PER_SEC) // Should be longer than the LED-only run time 
#define TICKS_STABLE_FOR_OFF_TO_SLOW                (TICKS_PER_SEC/2)
#define TICKS_STABLE_FOR_SLOW_TO_FAST               (4 * TICKS_PER_SEC) // should be longer than one RF packet


// Typedefs

typedef enum
{
    CAP_STATE_BOOTUP,
    CAP_STATE_CHARGING_OFF,
    CAP_STATE_CHARGING_SLOWLY,
    CAP_STATE_CHARGING_QUICKLY,
    CAP_STATE__NUM_STATES,
} cap_charging_state_t;

// Variables
static cap_charging_state_t mCapStateMachineState = CAP_STATE_BOOTUP;

static uint32_t mTicksAtStateEntry = 0;

static bool mLastCharging = false;

static bool mForceChargingStop = false;

// Implementations

// Check if we're in danger of overcharging the cap
static bool supercap_charge_too_high(void)
{
    bool tooHigh = false;
    
    if (gVcc > (SUPERCAP_MAX_MV + DIODE_DROP_MIN))
    {
        // We have a high voltage, and we're currently charging, so check
        // that we're not overcharging the cap (i.e., exceeding 3300 mV)
        uint8_t countsDown = ADC_read_supercap_relative();

        uint8_t threshold = (uint8_t)((gVcc - (SUPERCAP_MAX_MV + DIODE_DROP_MIN)) / MV_TO_COUNTS_FOR_RELATIVE_SUPERCAP);

        if (countsDown <= threshold)
        {
            // Voltage is too high (implying cap is already pretty highly charged anyway)
            tooHigh = true;
        }
    }    
    
    return tooHigh;
}

// Force supercap charging to stop temporarily
void SUPERCAP_force_charging_off(void)
{
    mForceChargingStop = true;
    SUPERCAP_charge();
}

// Update the state machine. Returns true if charging in any way, false otherwise
bool SUPERCAP_charge(void)
{
    static uint8_t sTicksVoltageGoodForUpshift = 0;
    bool isCharging = mLastCharging;
    cap_charging_state_t newState = mCapStateMachineState;
    
    // Action based on the current state, including updates to the state
    switch (mCapStateMachineState)
    {
        case CAP_STATE_BOOTUP:
            if (gTickCount > TICKS_BOOTUP_TO_OFF)
            {
                newState = CAP_STATE_CHARGING_OFF;
            }
            else if (gPrefsCache.selfTestEn)
            {
                newState = CAP_STATE_CHARGING_QUICKLY;
            }
            break;
        case CAP_STATE_CHARGING_OFF:
            if (gVcc > SUPERCAP_CHRG_THRESH_OFF_TO_SLOW_MIN)
            {
                // Have we had a stable voltage long enough to justify starting charging?
                if (sTicksVoltageGoodForUpshift > TICKS_STABLE_FOR_OFF_TO_SLOW)
                {
                    newState = CAP_STATE_CHARGING_SLOWLY;
                    sTicksVoltageGoodForUpshift = 0;
                }
                else
                {
                    sTicksVoltageGoodForUpshift++;
                }
            }
            else
            {
                // Voltage hasn't been stable enough
                sTicksVoltageGoodForUpshift = 0;
            }
            break;
        case CAP_STATE_CHARGING_SLOWLY:
            if (gVcc < SUPERCAP_CHRG_THRESH_SLOW_TO_OFF_UNDER ||
                mForceChargingStop ||
                supercap_charge_too_high())
            {
                newState = CAP_STATE_CHARGING_OFF;
            }
            else if (gVcc > SUPERCAP_CHRG_THRESH_SLOW_TO_FAST)   
            {
                if (sTicksVoltageGoodForUpshift > TICKS_STABLE_FOR_SLOW_TO_FAST)
                {
                    newState = CAP_STATE_CHARGING_QUICKLY;
                    sTicksVoltageGoodForUpshift = 0;
                }
                else
                {
                    sTicksVoltageGoodForUpshift++;
                }
            }
            else
            {
                sTicksVoltageGoodForUpshift = 0;
            }
            break;
        case CAP_STATE_CHARGING_QUICKLY:
            // Don't allow us to leave the quick-charging state if we're in self-test mode unless we're
            // going to overcharge the cap or we're trying to get out of the mode
            if (gPrefsCache.selfTestEn)
            {
                if (supercap_charge_too_high() || mForceChargingStop)
                {
                    newState = CAP_STATE_CHARGING_OFF;
                }
            }
            else
            {
                if (gVcc < SUPERCAP_CHRG_THRESH_FAST_TO_OFF_UNDER ||
                    mForceChargingStop ||
                    supercap_charge_too_high())
                {
                    newState = CAP_STATE_CHARGING_OFF;
                }
                else if (gVcc < SUPERCAP_CHRG_THRESH_FAST_TO_SLOW)
                {
                    newState = CAP_STATE_CHARGING_SLOWLY;
                }
            }
            break;
        default:
            break;
    }
    
    // Action based on the new state
    if (newState != mCapStateMachineState)
    {
        switch (newState)
        {
            case CAP_STATE_BOOTUP:
                break;
            case CAP_STATE_CHARGING_OFF:
                // Float the charge pin
                LATC = (LATC & ~(SUPERCAP_MED_CHRG_PIN));
                TRISCbits.TRISC7 = 1;
                WPUC7 = 0;
                break;
            case CAP_STATE_CHARGING_SLOWLY:
                // Slow-charge cap (weak pull-up)
                LATC = (LATC & ~(SUPERCAP_MED_CHRG_PIN));
                TRISCbits.TRISC7 = 1;
                WPUC7 = 1;
                isCharging = true;
                break;
            case CAP_STATE_CHARGING_QUICKLY:
                // Fast-charge cap (push-pull through 3.3k)
                LATC = LATC | SUPERCAP_MED_CHRG_PIN; // RC7 = 1; // use LATC in case the starting level is low
                TRISCbits.TRISC7 = 0;
                WPUC7 = 0;
                isCharging = true;
                break;
            default:
                break;
        }
        
        mTicksAtStateEntry = gTickCount;
        mCapStateMachineState = newState;
    }
    
    isCharging = (mCapStateMachineState == CAP_STATE_CHARGING_SLOWLY ||
                  mCapStateMachineState == CAP_STATE_CHARGING_QUICKLY);

    DEBUG_VALUE(isCharging);
    
    mForceChargingStop = false;
                
    return isCharging;
}

