#include "supercap.h"
#include "global.h"
#include "adc.h"
#include "prefs.h"

// Macros and constants

// Supercap charging action thresholds [mV]
#define SUPERCAP_CHRG_THRESH_OFF_TO_SLOW_MIN        (2700)
#define SUPERCAP_CHRG_THRESH_OFF_TO_SLOW_MAX        (3600)

#define SUPERCAP_CHRG_THRESH_SLOW_TO_OFF_UNDER      (2500)
#define SUPERCAP_CHRG_THRESH_SLOW_TO_OFF_OVER       (3600)
#define SUPERCAP_CHRG_THRESH_SLOW_TO_FAST           (2950)


#define SUPERCAP_CHRG_THRESH_FAST_TO_OFF_OVER       (3600)
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
            break;
        case CAP_STATE_CHARGING_OFF:
            if (gVcc > SUPERCAP_CHRG_THRESH_OFF_TO_SLOW_MIN &&
                gVcc < SUPERCAP_CHRG_THRESH_OFF_TO_SLOW_MAX)
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
                gVcc > SUPERCAP_CHRG_THRESH_SLOW_TO_OFF_OVER ||
                mForceChargingStop)
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
            if (gVcc > SUPERCAP_CHRG_THRESH_FAST_TO_OFF_OVER ||
                gVcc < SUPERCAP_CHRG_THRESH_FAST_TO_OFF_UNDER ||
                mForceChargingStop)
            {
                newState = CAP_STATE_CHARGING_OFF;
            }
            else if (gVcc < SUPERCAP_CHRG_THRESH_FAST_TO_SLOW)
            {
                newState = CAP_STATE_CHARGING_SLOWLY;
            }
            break;
        default:
            break;
    }
    
    // Other actions that override state-based decisions
    if (!gPrefsCache.supercapChrgEn)
    {
        newState = CAP_STATE_CHARGING_OFF;
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
    
    mForceChargingStop = false;
                
    return isCharging;
}

