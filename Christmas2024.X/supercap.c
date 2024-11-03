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

#define TICKS_TO_CALL_STABLE                        (5 * TICKS_PER_SEC)


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

// Implementations


// Update the state machine. Returns true if charging in any way, false otherwise
bool SUPERCAP_charge(void)
{
    bool isCharging = mLastCharging;
    cap_charging_state_t newState = mCapStateMachineState;
    
    // Action based on the current state, including updates to the state
    switch (mCapStateMachineState)
    {
        case CAP_STATE_BOOTUP:
            if (gTickCount > (2 * TICKS_PER_SEC))
            {
                newState = CAP_STATE_CHARGING_OFF;
            }
            break;
        case CAP_STATE_CHARGING_OFF:
            if (gVcc > SUPERCAP_CHRG_THRESH_OFF_TO_SLOW_MIN &&
                gVcc < SUPERCAP_CHRG_THRESH_OFF_TO_SLOW_MAX &&
                (gTickCount - mTicksAtStateEntry) > TICKS_TO_CALL_STABLE)
            {
                newState = CAP_STATE_CHARGING_SLOWLY;
            }
            break;
        case CAP_STATE_CHARGING_SLOWLY:
            if (gVcc < SUPERCAP_CHRG_THRESH_SLOW_TO_OFF_UNDER ||
                gVcc > SUPERCAP_CHRG_THRESH_SLOW_TO_OFF_OVER)
            {
                newState = CAP_STATE_CHARGING_OFF;
            }
            else if (gVcc > SUPERCAP_CHRG_THRESH_SLOW_TO_FAST &&
                    (gTickCount - mTicksAtStateEntry) > TICKS_TO_CALL_STABLE)
            {
                newState = CAP_STATE_CHARGING_QUICKLY;
            }
            break;
        case CAP_STATE_CHARGING_QUICKLY:
            if (gVcc > SUPERCAP_CHRG_THRESH_FAST_TO_OFF_OVER ||
                gVcc < SUPERCAP_CHRG_THRESH_FAST_TO_OFF_UNDER)
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
                
    return isCharging;
}

