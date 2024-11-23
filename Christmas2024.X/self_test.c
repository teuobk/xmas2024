#include "self_test.h"
#include "global.h"
#include "prefs.h"
#include "rf.h"
#include "supercap.h"
#include "adc.h"

// Macros and constants

#define VCC_USB_LDO_MIN_MV              (3100) // Can droop this low when charging a fully discharged supercap, especially on first boot
#define VCC_USB_LDO_MAX_MV              (3400)
#define VCC_USB_LDO_STABLE_TICKS        (1 * TICKS_PER_SEC)

// Supercap voltages are those observed while fast-charging and include the
// forward drop of the charging diode (e.g., a value of 600 mV here implies that
// the supercap's actual voltage is something like 300 mV)
// See the supercap module for more info about how these deltas are measured
// and calculated, including additional notes and caveats.
#define SUPERCAP_MAX_GOOD_DELTA         (210) // This value is roughly equivalent to a supercap voltage of 300 mV (600 mV including diode drop) when Vcc is 3300 mV
#define SUPERCAP_MIN_GOOD_DELTA         (6) // This value is roughly equivalent to a supercap voltage of 2950 mV (3200 mV including minimal diode drop) when Vcc is 3300 mV
#define SUPERCAP_STABLE_TICKS           (1 * TICKS_PER_SEC)

#define RADIO_RF_LEVEL_RATIO_OF_VCC     (128) // roughly 1650 mV when Vcc=3300 mV

// Typedefs

// Variables

static self_test_step_t mSelfTestState = STS_USB_LDO;

// Implementations

// The self-test state machine
void SELF_TEST_state_machine_update(void)
{
    static uint32_t mTicksInCurrentState = 0;
    self_test_step_t newState = mSelfTestState;

    // Figure out if we need to change to a new state based on new info
    switch (mSelfTestState)
    {
        case STS_USB_LDO:
            // Check that the the voltage rail is consistent with being powered
            // consistently via USB: look for the expected voltage after one
            // diode drop, and look for it to be relatively stable (to eliminate
            // confusion with, e.g., an RF source)
            if (gVcc > VCC_USB_LDO_MIN_MV &&
                gVcc < VCC_USB_LDO_MAX_MV)
            {
                if (mTicksInCurrentState > VCC_USB_LDO_STABLE_TICKS)
                {
                    newState = STS_SUPERCAP;
                }
            }
            else
            {
                // Restart the state timer if we're out of range
                mTicksInCurrentState = 0;
            }
            break;
        case STS_SUPERCAP:
            // Wait for the supercap to be charged up to a point where it's a
            // few hundred millivolts up from being dead flat. Use the same technique
            // we use to protect the supercap from overcharging to indirectly
            // monitor its voltage even from behind a diode. Look for evidence 
            // that the supercap is able to charge (thus showing both that it is
            // connected and that it isn't shorted). Also look for evidence that 
            // the voltage is down somewhat (at least a few hundred millivolts)
            // from Vcc to ensure that the 3.3k charging resistor is connected 
            // and as a second part of the check for the supercap to actually 
            // be present. While it might seem that this second check for a 
            // "not too high" voltage wouldn't work when the supercap is fully
            // charged, in reality, the supercap won't charge that fast on a
            // brand-new board, and even on a used board it's likely that the
            // supercap will discharge somewhat within a very reasonable period
            // of time.
            // TODO
            {   
                uint8_t latestSupercapDelta = SUPERCAP_get_latest_voltage_delta();
                
                // NOTE: latestSupercapDelta is automatically zero if charging
                // is not currently occuring
                
                if (latestSupercapDelta > SUPERCAP_MIN_GOOD_DELTA &&
                    latestSupercapDelta < SUPERCAP_MAX_GOOD_DELTA)
                {
                    if (mTicksInCurrentState > SUPERCAP_STABLE_TICKS)
                    {
                        newState = STS_RADIO;
                    }
                }
                else
                {
                    mTicksInCurrentState = 0;
                }
            }
            break;
        case STS_RADIO:
            // Wait for a level on the RF tap that is somewhere midway between 
            // VCC and ground, biased towards the high side (like perhaps 75% of
            // VCC when the board is powered from USB). This will show that the 
            // RF receiver is connected, that the matching network is likely
            // correct, and that both diodes are also likely populated correctly
            if (RF_get_latest_slicer_level() > RADIO_RF_LEVEL_RATIO_OF_VCC)
            {
                newState = STS_COMPLETE;
            }
            break;
        case STS_COMPLETE: // intentional fall-through
        default:
            break;
    }
    
    // Take action based on the new state if we just changed states
    if (newState != mSelfTestState)
    {
        switch (newState)
        {
            case STS_USB_LDO:
                break;
            case STS_SUPERCAP:
                break;
            case STS_RADIO:
                break;
            case STS_COMPLETE:
                PREFS_self_test_saved_state(false);
                break;
            default:
                break;
        }
        
        mSelfTestState = newState;
        mTicksInCurrentState = 0;
    }
    else
    {
        mTicksInCurrentState++;
    }
    
    // Automatically time out self-test mode even if we haven't passed everything
    // just in case we accidentally got into the self-test mode. Note that the self-
    // test flag is ordinarily cleared when the self-test passes, too.
    if (gTickCount > SELF_TEST_TIMEOUT_TICKS)
    {
        PREFS_self_test_saved_state(false);
    }
}

// Returns the current state of the self-test state machine
self_test_step_t SELF_TEST_get_current_step(void)
{
    return mSelfTestState;
}
