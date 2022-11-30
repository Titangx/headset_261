/*!
\copyright  Copyright (c) 2019 - 2021 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.

\version    %%version
\file       headset_sm.c
\brief     headset application SM .

*/

/* local includes */
#include "headset_sm.h"
#include "headset_sm_private.h"
#include "adk_log.h"
#include "headset_led.h"
#include "wired_audio_source.h"
#include "headset_test.h"
#include "headset_usb.h"
#include "headset_wired_audio_controller.h"

/* framework includes */
#include <ui.h>
#include <unexpected_message.h>
#include <connection_manager.h>
#include <connection_manager_config.h>
#include <hfp_profile.h>
#include <av.h>
#include <power_manager.h>
#include <power_manager_conditions.h>
#include <charger_monitor.h>
#include <headset_topology.h>
#include <pairing.h>
#include <device.h>
#include <device_properties.h>
#include <dfu.h>
#include <gaia_framework.h>
#include <handset_service.h>
#include <device_list.h>
#include <system_state.h>
#include <hfp_profile_config.h>
#include <usb_device.h>
#include <gatt_server_gatt.h>
#include <anc_state_manager.h>
#include <aec_leakthrough.h>
#include <system_reboot.h>
#include <ps_key_map.h>

#ifdef INCLUDE_FAST_PAIR
#include <fast_pair.h>
#endif

/* system includes */
#include <vm.h>
#include <vmtypes.h>
#include <panic.h>
#include <message.h>
#include <ps.h>

/* Make the type used for message IDs available in debug tools */
LOGGING_PRESERVE_MESSAGE_ENUM(sm_internal_message_ids)

#define ATTRIBUTE_BASE_PSKEY_INDEX  100
#define TDL_BASE_PSKEY_INDEX        142
#define TDL_INDEX_PSKEY             141
#define TDL_SIZE                    DeviceList_GetMaxTrustedDevices()

/*!< headset state machine. */
smTaskData headset_sm;

const message_group_t sm_ui_inputs[] =
{
    UI_INPUTS_HANDSET_MESSAGE_GROUP,
    UI_INPUTS_DEVICE_STATE_MESSAGE_GROUP,
    UI_INPUTS_APP_MESSAGE_GROUP
};

/*! \brief Get the state machine disconnect lock. */
#define headsetSmGetDisconnectLock() (SmGetTaskData()->disconnect_lock)
/*! \brief Clear the disconnect lock */
#define headsetSmClearDisconnectLock() (headsetSmGetDisconnectLock() = FALSE)
/*! \brief Set the disconnect lock */
#define headsetSmSetDisconnectLock() (headsetSmGetDisconnectLock() = TRUE)
/*! @brief Query if pairing has been initiated by the user. */
#define headsetSmIsUserPairing() (SmGetTaskData()->user_pairing)
/*! @brief Set user initiated pairing flag. */
#define headsetSmSetUserPairing()  (SmGetTaskData()->user_pairing = TRUE)
/*! @brief Clear user initiated pairing flag. */
#define headsetSmClearUserPairing()  (SmGetTaskData()->user_pairing = FALSE)
/*! @brief Query auto poweron */
#define headsetSmGetAutoPowerOn() (SmGetTaskData()->auto_poweron)
/*! @brief Set headset auto poweron flag. */
#define headsetSmSetAutoPowerOn()  (SmGetTaskData()->auto_poweron = TRUE)
/*! @brief Clear auto poweron flag. */
#define headsetSmClearAutoPowerOn()  (SmGetTaskData()->auto_poweron = FALSE)

/*! \brief Return TRUE if the state is idle */
#define headsetSmStateIsIdle(state) (state == HEADSET_STATE_IDLE)

/*! \brief Return TRUE if there is no handset connection */
#define IsBtConnectionIdle() (headsetSmStateIsIdle(headsetGetState()) && (!appDeviceIsHandsetConnected()) && \
                                                                          headsetConfigIdleTimeoutMs())
/*! \brief Return TRUE if the idle timer needs to start */
#define headsetSmIsIdleTimerNeedToStart()  (IsBtConnectionIdle() && (!IsWiredAudioActive()) && \
                           (!AncStateManager_IsEnabled()) && (!AecLeakthrough_IsLeakthroughEnabled()))

/*! \brief Return TRUE if A2DP state is 'connected media streaming' and avrcp status is paused  */
#define IsA2dpStreamingAndAvrcpPaused() ( Av_IsA2dpSinkStreaming() && Av_IsPaused())

/*! \brief Return TRUE if the state is pairing*/
#define headsetSmStateIsPairing(state) (state == HEADSET_STATE_PAIRING)

/*! \brief Check if the headset is active.
*/
#define headsetSmIsActiveState(state) ((state == HEADSET_STATE_IDLE) || \
                                                      (state == HEADSET_STATE_BUSY))

/*! \brief Return TRUE if Auto power on panic is enabled and last reset happened due to panic */
#define headsetSmIsLastResetDueToPanic()  ( appConfigEnableAutoPowerOnAfterPanic() && isHeadsetResetSourcePanic() )


/*! \brief Check if the Bt audio is active.
*/                                                      
#define IsBtAudioActive() (Av_IsA2dpSinkStreaming() || HfpProfile_IsScoActive())

/*! \brief Check if the wired audio is active.
*/
#define IsWiredAudioActive() (WiredAudioSource_IsAudioAvailable(audio_source_line_in) || HeadsetUsb_IsAudioConnected())

/*! \brief Check if the headset audio is active.
*/
#define IsHeadsetAudioActive() ((IsBtAudioActive()) || (IsWiredAudioActive()))

#ifdef INCLUDE_DFU
/*! \brief Check if the headset upgrade is active
*/
#define headsetSmIsDfuActive(reboot_reason) (reboot_reason == REBOOT_REASON_DFU_RESET)
#endif /* INCLUDE_DFU */


static void headsetSmHandleAncUpdateStateEnableInd(void);
static void headsetSmHandleAncUpdateStateDisableInd(void);
static void headsetSetState(headsetState new_state);
static headsetState headsetGetState(void);
static void headsetSMStopIdleTimer(void);
static void headsetSMStartIdleTimer(void);

static void headsetSmHandleAncUpdateStateEnableInd(void)
{
    if(AncStateManager_IsEnabled())
    {
        headsetSMStopIdleTimer();
    }
}

static void headsetSmHandleAncUpdateStateDisableInd(void)
{
    if(headsetSmIsIdleTimerNeedToStart())
    {
        headsetSMStartIdleTimer();
    }
}

/*! \brief Function to start or stop headset idle timer for leakthrough*/
static void headsetSmHandleLeakthroughStateInd(LEAKTHROUGH_UPDATE_STATE_IND_T *msg)
{
    if(msg->state)
    {
        headsetSMStopIdleTimer();
    }
    else
    {
        if (headsetSmIsIdleTimerNeedToStart())
        {
            headsetSMStartIdleTimer();
        }
    }
}

/*! \brief function to start headset idle timer */
static void headsetSMStartIdleTimer(void)
{
    if(headsetConfigIdleTimeoutMs() && headsetSmIsIdleTimerNeedToStart())
    {
       MessageCancelAll(headsetSmGetTask(), SM_INTERNAL_TIMEOUT_IDLE);
       MessageSendLater(headsetSmGetTask(), SM_INTERNAL_TIMEOUT_IDLE, NULL, headsetConfigIdleTimeoutMs()); 
    }
}

/*! \brief function to stop headset idle timer */
static void headsetSMStopIdleTimer(void)
{
    MessageCancelAll(headsetSmGetTask(), SM_INTERNAL_TIMEOUT_IDLE);
}

/*! \brief function to check if the last reset was caused by Panic */
static bool isHeadsetResetSourcePanic(void)
{
    vm_reset_source source = VmGetResetSource();
    if(source == RESET_SOURCE_PANIC || source == RESET_SOURCE_APP_PANIC)
    {
        return TRUE;
    }
    return FALSE;
}

/*! \brief Prepare for streaming wired audio */
static bool headsetPrepareForWiredAudio(void)
{
    /* Disable wired audio controller */
    HeadsetWiredAudioController_Disable();
	
    /* Disable Headset topology */
    HeadsetTopology_Stop(headsetSmGetTask());
    return TRUE;
}

/*! \brief Prepare for streaming Bluetooth audio */
static bool headsetPrepareForBtAudio(void)
{
    /* Enable Headset topology */
    HeadsetTopology_Start(headsetSmGetTask());
    return TRUE;
}

/*! \brief Initiate disconnect of all links */
static void headsetSmInitiateLinkDisconnection(uint16 timeout_ms)
{
    bool disconnecting_link = headsetSmDisconnectLink();

    DEBUG_LOG_VERBOSE("headsetSmInitiateLinkDisconnection");

    if (!disconnecting_link)
    {
        headsetSmClearDisconnectLock();
        DEBUG_LOG_VERBOSE("headsetSmInitiateLinkDisconnection: Lock cleared");
    }
    else
    {
        headsetSmSetDisconnectLock();
        DEBUG_LOG_VERBOSE("headsetSmInitiateLinkDisconnection: Lock set");
    }

    MessageSendConditionally(headsetSmGetTask(), SM_INTERNAL_LINK_DISCONNECTION_COMPLETE,
                             NULL, &headsetSmGetDisconnectLock());

    /* Start a timer to force reset if we fail to complete disconnection */
    MessageSendLater(headsetSmGetTask(), SM_INTERNAL_TIMEOUT_LINK_DISCONNECTION, NULL, timeout_ms);
}

static void headsetSmClearPsStore(void)
{
    DEBUG_LOG_FN_ENTRY("headsetSmClearPsStore");

    for (int i=0; i<TDL_SIZE; i++)
    {
        PsStore(ATTRIBUTE_BASE_PSKEY_INDEX+i, NULL, 0);
        PsStore(TDL_BASE_PSKEY_INDEX+i, NULL, 0);
    }

    PsStore(TDL_INDEX_PSKEY, NULL, 0);
    PsStore(PS_KEY_HFP_CONFIG, NULL, 0);

    /* Clear out any in progress DFU status */
#ifdef INCLUDE_DFU
    Dfu_ClearPsStore();
#endif
}

/*! \brief Delete handset pairing and reboot device. */
static void headsetSmDeletePairingAndReset(void)
{
    DEBUG_LOG_ALWAYS("headsetSmDeletePairingAndReset");

    /* cancel the link disconnection, may already be gone if it fired to get us here */
    MessageCancelFirst(headsetSmGetTask(), SM_INTERNAL_TIMEOUT_LINK_DISCONNECTION);

    headsetSmClearPsStore();

#ifdef INCLUDE_FAST_PAIR
    /* Delete the account keys */
    FastPair_DeleteAccountKeys();
#endif

    SystemReboot_Reboot();
}

/*! \brief Handle indication all requested links are now disconnected. */
static void headsetSmHandleInternalLinkDisconnectionComplete(void)
{
    DEBUG_LOG_DEBUG("headsetSmHandleInternalLinkDisconnectionComplete 0x%x", headsetGetState());
    MessageCancelFirst(headsetSmGetTask(), SM_INTERNAL_TIMEOUT_LINK_DISCONNECTION);

    /* Delete all devices for headset device type. */
    BtDevice_DeleteAllDevicesOfType(DEVICE_TYPE_HANDSET);

#ifdef INCLUDE_FAST_PAIR
    /* Delete the account keys */
    FastPair_DeleteAccountKeys();
#endif

}

/*! \brief Handle failure to successfully disconnect links within timeout.
*/
static void headsetSmLinkDisconnectionTimeout(void)
{
    DEBUG_LOG_DEBUG("headsetSmLinkDisconnectionTimeout 0x%x, Lock = %d", headsetGetState(), headsetSmGetDisconnectLock());

    if(headsetSmGetDisconnectLock())
    {
        MessageCancelAll(headsetSmGetTask(), SM_INTERNAL_LINK_DISCONNECTION_COMPLETE);
        headsetSmClearDisconnectLock();
    }
}

/*! \brief Start/Restart headset limbo timer, if its not zero */
static void headsetSmStartLimboTimer(void)
{
    if(headsetConfigLimboTimeoutMs())
    {
        DEBUG_LOG_DEBUG("headsetSmStartLimboTimer : Limbo Timer started LimboTimeoutMs %u",headsetConfigLimboTimeoutMs());
        MessageCancelAll(headsetSmGetTask(), SM_INTERNAL_TIMEOUT_LIMBO);
        MessageSendLater(headsetSmGetTask(), SM_INTERNAL_TIMEOUT_LIMBO, NULL, headsetConfigLimboTimeoutMs());
    }
}

/*! \brief Stop headset limbo timer. */
static void headsetSmStopLimboTimer(void)
{
    DEBUG_LOG_DEBUG("headsetSmStopLimboTimer : Limbo Timer stopped");
    MessageCancelAll(headsetSmGetTask(), SM_INTERNAL_TIMEOUT_LIMBO);
}

/*! \brief Take action following chargers indication of charger disconnect */
static void headsetSmHandleChargerMessageDetached(void)
{
    DEBUG_LOG_VERBOSE("headsetSmHandleChargerMessageDetached , state %d", headsetGetState());
    if(HEADSET_STATE_LIMBO ==  headsetGetState())
    {
        headsetSmStartLimboTimer();
    }
}

/*! \brief Take action following power's indication of imminent shutdown.
    Can be received in any state. */
static void headsetSmHandlePowerShutdownPrepareInd(void)
{
    DEBUG_LOG_VERBOSE("headsetSmHandlePowerShutdownPrepareInd, state %d", headsetGetState()); 
    switch (headsetGetState())
    {
        case HEADSET_STATE_LIMBO:
            DEBUG_LOG_VERBOSE("headsetSmHandlePowerShutdownPrepareInd, SilentCommitEnabled %d", Dfu_IsSilentCommitEnabled());
            /* Do not respond to shut down request if silent commit is pending
             * because DFU reboot will be in progress and that will get
             * interrupted.
             */
            if(!Dfu_IsSilentCommitEnabled())
            {
                appPowerShutdownPrepareResponse(headsetSmGetTask());
            }
            break;
        default : 
            headsetSetState(HEADSET_STATE_TERMINATING);
            break;
    }
}

/*! \brief Request a factory reset. */
static void headsetSmFactoryReset(void)
{
    MessageSend(headsetSmGetTask(), SM_INTERNAL_FACTORY_RESET, NULL);
}

/*! \brief Request handset pair. */
static void headsetSmPairHandset(void)
{
    MessageSend(headsetSmGetTask(), SM_INTERNAL_PAIR_HANDSET, NULL);
}

/*! \brief Request handset connection. */
static void headsetSmConnectHandset(void)
{
    if (!IsWiredAudioActive())
    {
        DEBUG_LOG_DEBUG("appSmConnectHandset");
        HeadsetTopology_ConnectMruHandset();
    }
}

static void headsetSmDisconnectLruHandset(void)
{
    DEBUG_LOG_DEBUG("headsetSmDisconnectLruHandset");
    HeadsetTopology_DisconnectLruHandset();
}

/*! \brief Request handset delete. */
static void headsetSmDeleteHandsets(void)
{
    MessageSend(headsetSmGetTask(), SM_INTERNAL_DELETE_HANDSETS, NULL);
}

/*! \brief Request headset power off. */
static void headsetSmPowerOff(void)
{
    MessageSend(headsetSmGetTask(), SM_INTERNAL_POWER_OFF, NULL);
}

/*! \brief Handle power on confirmation
 */
static void headsetSmHandlePoweredOn(void)
{
    if(!IsWiredAudioActive())
    {
        if (BtDevice_IsPairedWithHandset())
        {
            DEBUG_LOG_DEBUG("headsetSmHandlePoweredOn, already paired with handset");

            /* Move to idle state and initiate connection */
            headsetSetState(HEADSET_STATE_IDLE);
            return;
        }
        else
        {
            DEBUG_LOG_DEBUG("headsetSmHandlePoweredOn, no device in PDL, move to pairing");

            /* Stop Wired Audio PIO monitoring */
            WiredAudioSource_StopMonitoring(headsetSmGetTask());

            /* Disable USB Audio Feature */
            HeadsetUsb_AudioDisable(headsetSmGetTask());

            headsetSmClearUserPairing();
            headsetSetState(HEADSET_STATE_PAIRING);
        }
    }
    else
    {
        /* Changing state to HEADSET_STATE_IDLE if wired audio source connected message is not received.
        This will handle the case of 'Wired audio source is removed before sending connected message' */
        if(headsetGetState() == HEADSET_STATE_POWERING_ON)
        {
            headsetSetState(HEADSET_STATE_IDLE);
        }
    }
}

/*! \brief Idle timeout */
static void headsetSmHandleTimeoutIdle(void)
{
    DEBUG_LOG_DEBUG("headsetSmHandleTimeoutIdle, state %d, handset connected %d", headsetGetState(), appDeviceIsHandsetConnected());
    if((HEADSET_STATE_IDLE == headsetGetState()) && (!appDeviceIsHandsetConnected()))
    {
        headsetSmPowerOff();
    }
}

/*! \brief Limbo timeout */
static void headsetSmHandleTimeoutLimbo(void)
{
    DEBUG_LOG_DEBUG("headsetSmHandleTimeoutLimbo, state %d", headsetGetState());
    if((HEADSET_STATE_LIMBO == headsetGetState()))
    {
        appPowerOffRequest();
    }
}

#ifdef INCLUDE_DFU
/*! \brief Check if DFU was active.
 */
static void headsetCheckDfu(void)
{
    /* Limbo to power-on transition should happen only for interactive commit.
     * Silent commit happens when user puts HS in limbo state so, the same
     * state should be retained even after reboot. */
    if(headsetSmIsDfuActive(Dfu_GetRebootReason()) && !Dfu_IsSilentCommitEnabled())
    {
        /*
         * Safe restart/stretch of DFU reconnection timer as limbo to power-on
         * transition is delayed.
         */
        UpgradeRestartReconnectionTimer();
        Ui_InjectUiInputWithDelay(ui_input_sm_power_on,
                                headsetConfigDfuCommitDelayForLimboToPowerOn());
        DEBUG_LOG_DEBUG("headsetCheckDfu: Reboot Reason: %d", Dfu_GetRebootReason());
    }
}
#endif

/*****************************************************************************
 * SM state transition handler functions
 *****************************************************************************/

/*! \brief Enter initialising state

    This function is called whenever the state changes to
    HEADSET_STATE_LIMBO.
    It is reponsible for initialising global aspects of the application,
    i.e. non application module related state.
*/
static void headsetEnterLimbo(void)
{
    DEBUG_LOG_DEBUG("headsetEnterLimbo : HEADSET_STATE_LIMBO");
    headsetSmStartLimboTimer();
#ifdef INCLUDE_DFU
    headsetCheckDfu();
    
    /* check and inform the DFU domain if the headset is in limbo state (not-in-use state) 
     * and silent commit is enabled */
    if(Dfu_IsSilentCommitEnabled())
    {
        Dfu_HandleDeviceNotInUse();
    }
#endif
    if(headsetSmGetAutoPowerOn())
    {
        DEBUG_LOG_DEBUG("headsetEnterLimbo : Auto PowerOn");
        headsetSmClearAutoPowerOn();
        Ui_InjectUiInput(ui_input_sm_power_on);
    }
}

/*! \brief Exit limbo state.
 */
static void headsetExitLimbo(void)
{
    DEBUG_LOG_DEBUG("headsetExitLimbo");
    headsetSmStopLimboTimer();
}

/*! \brief Enter powering on state.
 */
static void headsetEnterPoweringOn(void)
{
    DEBUG_LOG_ALWAYS("headsetEnterPoweringOn : HEADSET_STATE_POWERING_ON");
}

/*! \brief Exit powering on state.
 */
static void headsetExitPoweringOn(void)
{
    DEBUG_LOG_DEBUG("headsetExitPoweringOn");
}

/*! \brief Enter powering off state.
 */
static void headsetEnterPoweringOff(void)
{
    DEBUG_LOG_ALWAYS("headsetEnterPoweringOff : HEADSET_STATE_POWERING_OFF");
}

/*! \brief Exit powering off state.
 */
static void headsetExitPoweringOff(void)
{
    DEBUG_LOG_DEBUG("headsetExitPoweringOff");
}

/*! \brief Enter actions when we enter the factory reset state.
 */
static void headsetEnterFactoryReset(void)
{
    DEBUG_LOG_DEBUG("headsetEnterFactoryReset : HEADSET_STATE_FACTORY_RESET");
    headsetSmDeletePairingAndReset();
}

/*! \brief Exit factory reset. */
static void headsetExitFactoryReset(void)
{
    /* Should never happen */
    Panic();
}

/*! \brief Enter Idle state.
 */
static void headsetEnterIdle(void)
{
    DEBUG_LOG_DEBUG("headsetEnterIdle : HEADSET_STATE_IDLE");

    if(headsetSmIsIdleTimerNeedToStart())
    {
        headsetSMStartIdleTimer();
    }

    Ui_InformContextChange(ui_provider_app_sm, context_app_sm_idle);
}

/*! \brief Exit Idle on state.
 */
static void headsetExitIdle(void)
{
    DEBUG_LOG_DEBUG("headsetExitIdle");

    headsetSMStopIdleTimer();

    Ui_InformContextChange(ui_provider_app_sm, context_app_sm_exit_idle);
}

/*! \brief Enter Busy state.
 */
static void headsetEnterBusy(void)
{
   DEBUG_LOG_DEBUG("headsetEnterBusy : HEADSET_STATE_BUSY");

   if(IsWiredAudioActive())
   {   
      headsetPrepareForWiredAudio();
   }
}

/*! \brief Exit Busy state.
 */
static void headsetExitBusy(void)
{
    DEBUG_LOG_DEBUG("headsetExitBusy");
    if(!IsWiredAudioActive())
    {
       headsetPrepareForBtAudio();
    }
}

/*! \brief Enter Pairing state.
 */
static void headsetEnterPairing(void)
{
    DEBUG_LOG_DEBUG("headsetEnterPairing : HEADSET_STATE_PAIRING");

    if(headsetSmIsUserPairing())
    {
        HeadsetTopology_ProhibitHandsetConnection(TRUE);
    }
    else
    {
        Pairing_Pair(headsetSmGetTask(), headsetSmIsUserPairing());
    }
    HandsetService_ConnectableRequest(headsetSmGetTask());
}

/*! \brief Exit Pairing on state.
 */
static void headsetExitPairing(void)
{
    DEBUG_LOG_DEBUG("headsetExitPairing");
    Pairing_PairStop(NULL);
    headsetSmClearUserPairing();
    HeadsetTopology_ProhibitHandsetConnection(FALSE);
}

/*! \brief Enter Terminating state.
 */
static void headsetEnterTerminating(void)
{
    DEBUG_LOG_DEBUG("headsetEnterTerminating : HEADSET_STATE_TERMINATING");
    bdaddr addr;
    if (appDeviceGetHandsetBdAddr(&addr) && HandsetService_IsBredrConnected(&addr))
    {
        ConManagerSendCloseAclRequest(&addr, TRUE);
    }
    WiredAudioSource_StopMonitoring(headsetSmGetTask());
    appPowerShutdownPrepareResponse(headsetSmGetTask());
}

/*! \brief Exit Terminating state.
 */
static void headsetExitTerminating(void)
{
    DEBUG_LOG_DEBUG("headsetExitTerminating");
}

/*! \brief Provides Headset Application state machine context changes to the User Interface module.

    \param[in]  void

    \return     current_sm_ctxt - current application context of sm module.
*/
static unsigned headsetSm_GetApplicationCurrentContext(void)
{
    sm_provider_context_t context = context_app_sm_powered_on;

     switch(headsetGetState())
     {
        case HEADSET_STATE_NULL : /* fall through */
        case HEADSET_STATE_LIMBO: /* fall through */
        case HEADSET_STATE_POWERING_ON: /* fall through */
        case HEADSET_STATE_POWERING_OFF: /* fall through */
        case HEADSET_STATE_TERMINATING: /* fall through */
        case HEADSET_STATE_FACTORY_RESET: context = context_app_sm_powered_off;
            break;

        case HEADSET_STATE_BUSY: /* fall through */
        case HEADSET_STATE_IDLE: context = appDeviceIsHandsetConnected() == TRUE ? context_app_sm_idle_connected : context_app_sm_idle;
            break;

        default : break;
     }

    return (unsigned)context;
}


/* This function is called to change the applications state, it automatically
   calls the entry and exit functions for the new and old states.
*/
static void headsetSetState(headsetState new_state)
{
    smTaskData* sm = SmGetTaskData();
    headsetState previous_state = SmGetTaskData()->state;

    DEBUG_LOG_STATE("headsetSetState, state 0x%02x to 0x%02x", previous_state, new_state);

    /* Handle state exit functions */
    switch (previous_state)
    {
        case HEADSET_STATE_NULL:
            /* This can occur when DFU is entered during INIT. */
            break;

        case HEADSET_STATE_LIMBO:
            headsetExitLimbo();
            break;

        case HEADSET_STATE_POWERING_ON:
            headsetExitPoweringOn();
            break;

        case HEADSET_STATE_POWERING_OFF:
            headsetExitPoweringOff();
            break;

        case HEADSET_STATE_FACTORY_RESET:
            headsetExitFactoryReset();
            break;

        case HEADSET_STATE_PAIRING:
            headsetExitPairing();
            break;

        case HEADSET_STATE_IDLE:
            headsetExitIdle();
            break;

        case HEADSET_STATE_BUSY:
            headsetExitBusy();
            break;

        case HEADSET_STATE_TERMINATING:
            headsetExitTerminating();
            break;          

        default:
            DEBUG_LOG_ERROR("Attempted to exit unsupported state 0x%02x", SmGetTaskData()->state);
            Panic();
            break;
    }
    /* Set new state */
    SmGetTaskData()->state = new_state;
    /* Handle state entry functions */
    switch (new_state)
    {
        case HEADSET_STATE_LIMBO:
            headsetEnterLimbo();
            break;

        case HEADSET_STATE_FACTORY_RESET:
            headsetEnterFactoryReset();
            break;

        case HEADSET_STATE_POWERING_ON:
            headsetEnterPoweringOn();
            break;

        case HEADSET_STATE_PAIRING:
            headsetEnterPairing();
            break;

        case HEADSET_STATE_IDLE:
            headsetEnterIdle();
            break;

        case HEADSET_STATE_POWERING_OFF:
            headsetEnterPoweringOff();
            break;

        case HEADSET_STATE_BUSY:
            headsetEnterBusy();
            break;
            
        case HEADSET_STATE_TERMINATING:
            headsetEnterTerminating();
            break;      

        default:
            DEBUG_LOG_ERROR("Attempted to enter unsupported state 0x%02x", new_state);
            Panic();
            break;
    }

    Ui_InformContextChange(ui_provider_app_sm, headsetSm_GetApplicationCurrentContext());
    DEBUG_LOG_VERBOSE("headsetSetState, new state 0x%02x", sm->state);
}

static headsetState headsetGetState(void)
{
    return SmGetTaskData()->state;
}

/*! \brief Handle request to start factory reset. */
static void headsetSmHandleInternalFactoryReset(void)
{
    if (headsetGetState() > HEADSET_STATE_POWERING_ON)
    {
        DEBUG_LOG_DEBUG("headsetSmHandleInternalFactoryReset");
        headsetSetState(HEADSET_STATE_FACTORY_RESET);
    }
    else
        DEBUG_LOG_WARN("headsetSmHandleInternalFactoryReset cannot be done in state %d", headsetGetState());
}


/*! \brief Handle request to start handset pair. */
static void headsetSmHandleInternalPairHandset(void)
{
    /* The A2DP media channel is not closed on avrcp pause for a few seconds. 
    Headset stays in busy state till the media channel is closed. Headset state 
    machine allows pair handset request in the above case as well. */
    if (headsetSmStateIsIdle(headsetGetState()) || IsA2dpStreamingAndAvrcpPaused())
    {
       /* Stop Wired Audio PIO monitoring */
       WiredAudioSource_StopMonitoring(headsetSmGetTask());

       /* Disable USB Audio Feature */
       HeadsetUsb_AudioDisable(headsetSmGetTask());

       DEBUG_LOG_DEBUG("headsetSmHandleInternalPairHandset USER PAIRING REQUEST");
       headsetSmSetUserPairing();
       headsetSetState(HEADSET_STATE_PAIRING);
    }
    else
        DEBUG_LOG_WARN("headsetSmHandleInternalPairHandset can only pair in IDLE state");
}

/*! \brief Delete pairing for all handsets.
    \note There must be no connections to a handset for this to succeed. */
static void headsetSmHandleInternalDeleteHandsets(void)
{
    DEBUG_LOG_DEBUG("headsetSmHandleInternalDeleteHandsets");

    switch (headsetGetState())
    {
        case HEADSET_STATE_IDLE:
        case HEADSET_STATE_BUSY:
            /* Stop being connectable when deleting handset pairing */
            HandsetService_CancelConnectableRequest(headsetSmGetTask());
            headsetSmInitiateLinkDisconnection(headsetConfigDisconnectTimeoutMs());
            break;

        default:
            DEBUG_LOG_WARN("headsetSmHandleInternalDeleteHandsets bad state %u",
                                                        headsetGetState());
            break;
    }
}

/*! \brief Handle request to power off headset. */
static void headsetSmHandleInternalPowerOff(void)
{
    if (headsetGetState() > HEADSET_STATE_LIMBO)
    {
        SystemState_PowerOff();
    }
}

/*! \brief Handle request to power on headset. */
static void headsetSmHandlePowerOn(void)
{
    if (headsetGetState() == HEADSET_STATE_LIMBO)
    {
        SystemState_PowerOn();
    }
}

/*! \brief handles sm module specific ui inputs

    Invokes routines based on ui input received from ui module.

    \param[in] id - ui input

    \returns void
 */
static void headsetSmHandleUiInput(MessageId ui_input)
{
    switch (ui_input)
    {
        case ui_input_sm_power_on:
            DEBUG_LOG_VERBOSE("headsetSmHandleUiInput received ui_input_sm_power_on");
            headsetSmHandlePowerOn();
            break;

        case ui_input_sm_power_off:
            DEBUG_LOG_VERBOSE("headsetSmHandleUiInput received ui_input_sm_power_off");
            headsetSmPowerOff();
            break;

        case ui_input_connect_handset:
             DEBUG_LOG_VERBOSE("headsetSmHandleUiInput received ui_input_connect_handset");
             headsetSmConnectHandset();
            break;

        case ui_input_disconnect_lru_handset:
            DEBUG_LOG_VERBOSE("headsetSmHandleUiInput received ui_input_disconnect_lru_handset");
            headsetSmDisconnectLruHandset();
            break;

        case ui_input_sm_pair_handset:
            DEBUG_LOG_VERBOSE("headsetSmHandleUiInput received ui_input_sm_pair_handset");
            headsetSmPairHandset();
            break;

        case ui_input_sm_delete_handsets:
            DEBUG_LOG_VERBOSE("headsetSmHandleUiInput received ui_input_sm_delete_handset");
            headsetSmDeleteHandsets();
            break;

        case ui_input_factory_reset_request:
            headsetSmFactoryReset();
            break;

        case ui_input_app_consume:
             DEBUG_LOG_VERBOSE("headsetSmHandleUiInput received ui_input_app_consume");
            break;

        default:
            break;
    }
}

/*! \brief Handle completion of application module initialisation. */
static void headsetSmHandleSystemSartedUpToLimbo(void)
{
    DEBUG_LOG_INFO("headsetSmHandleSystemSartedUpToLimbo");

    switch (headsetGetState())
    {
        case HEADSET_STATE_NULL:
            {
                headsetSetState(HEADSET_STATE_LIMBO);
            }
        break;

        default:
            Panic();
    }
}

static void headsetSmHandleSystemStateChange(SYSTEM_STATE_STATE_CHANGE_T *msg)
{
    DEBUG_LOG_DEBUG("headsetSmHandleSystemStateChange old state 0x%x, new state 0x%x", msg->old_state, msg->new_state);

    if(msg->old_state == system_state_starting_up && msg->new_state == system_state_limbo)
    {
        headsetSmHandleSystemSartedUpToLimbo();
    }
    else if(msg->old_state == system_state_limbo && msg->new_state == system_state_powering_on)
    {
        headsetSetState(HEADSET_STATE_POWERING_ON);
    }
    else if(msg->old_state == system_state_powering_on && msg->new_state == system_state_active)
    {
        headsetSmHandlePoweredOn();
    }
    else if(msg->old_state == system_state_active && msg->new_state == system_state_powering_off)
    {
        headsetSetState(HEADSET_STATE_POWERING_OFF);
    }
}

Task headsetSmGetTask(void)
{
  return &headset_sm.task;
}

/*! \brief Handle completion of handset pairing. */
static void headsetSmHandlePairingPairConfirm(PAIRING_PAIR_CFM_T *cfm)
{
    DEBUG_LOG_DEBUG("headsetSmHandlePairingPairConfirm, status %d", cfm->status);

    switch (headsetGetState())
    {
        case HEADSET_STATE_PAIRING:
            DEBUG_LOG_VERBOSE("PAIRING COMPLETE");

            /* Start Wired Audio PIO monitoring */    
            WiredAudioSource_StartMonitoring(headsetSmGetTask());

            /* Enable USB Audio Feature */
            HeadsetUsb_AudioEnable(headsetSmGetTask());

            headsetSetState(HEADSET_STATE_IDLE);
            break;

        case HEADSET_STATE_FACTORY_RESET:
            /* Nothing to do, even if pairing with handset succeeded, the final
            act of factory reset is to delete handset pairing */
            break;

        default:
            /* Ignore, paired with handset with known address as requested by peer */
            break;
    }
}

/*! \brief Handle notification of Handset Service connection. */
static void headsetSmHandleHandsetServiceConnectedInd(HANDSET_SERVICE_CONNECTED_IND_T* ind)
{
    UNUSED(ind);

    /* Inform UI that a handset has been connected */
    Ui_InformContextChange(ui_provider_app_sm, context_app_sm_idle_connected);
    headsetSMStopIdleTimer();
}

/*! \brief Handle handset disconnected indication from headset topology. */
static void headsetSmHandleHeadsetTopologyHandsetDisInd(HEADSET_TOPOLOGY_HANDSET_DISCONNECTED_IND_T* ind )
{
    DEBUG_LOG_DEBUG("headsetSmHandleHeadsetTopologyHandsetDisInd HEADSET_TOPOLOGY_HANDSET_DISCONNECTED_IND, status %d", ind->status);
    if(headsetSmIsUserPairing() && headsetSmStateIsPairing(headsetGetState()))
    {
        if(ind->status == handset_service_status_success)
        {
            Pairing_Pair(headsetSmGetTask(), TRUE);
        }
        else if(ind->status ==  handset_service_status_failed)
        {
            headsetSetState(HEADSET_STATE_IDLE);
        }
    }
}

static void headsetSmUpdateDisconnectingLink(void)
{
    /* Update the disconnecting handset link lock status */
    if(headsetSmGetDisconnectLock())
    {
        DEBUG_LOG_DEBUG("headsetSmUpdateDisconnectingLink disconnecting handset");
        if (!HandsetService_IsAnyBredrConnected())
        {
            DEBUG_LOG_DEBUG("headsetSmUpdateDisconnectingLink handset disconnected");
            headsetSmClearDisconnectLock();
            DEBUG_LOG_VERBOSE("headsetSmUpdateDisconnectingLink: Lock cleared");
        }
        else
        {
            DEBUG_LOG_DEBUG("headsetSmUpdateDisconnectingLink: Still connected");
        }
    }
}

/*! \brief Handle notification of handset disconnection. */
static void headsetSmHandleHandsetServiceDisconnectedInd(HANDSET_SERVICE_DISCONNECTED_IND_T *ind)
{
    DEBUG_LOG_DEBUG("headsetSmHandleHandsetServiceDisconnectedInd status %u", ind->status);
    headsetSmUpdateDisconnectingLink();

    if(headsetGetState() != HEADSET_STATE_PAIRING )
    {
        /* Inform UI that a handset has been disconnected */
        Ui_InformContextChange(ui_provider_app_sm, context_app_sm_idle);
    }
    if(headsetSmIsIdleTimerNeedToStart())
    {
        headsetSMStartIdleTimer();
    }
}

static headsetState headsetSmDetermineCoreState(void)
{
    bool busy = IsHeadsetAudioActive();
    headsetState current_state = headsetGetState();
    
    if ((HEADSET_STATE_IDLE == current_state) || (HEADSET_STATE_BUSY == current_state))
    {
        return busy ? HEADSET_STATE_BUSY:
                          HEADSET_STATE_IDLE;
    }
    else
    {
        return current_state;
    }
}

bool headetSmHandleTopologyStopCfm(Message message)
{
    UNUSED(message);
    DEBUG_LOG_VERBOSE("headetSmHandleTopologyStopCfm");

    if(headsetGetState() == HEADSET_STATE_POWERING_OFF)
    {
        /* Stop topology had called as part of power off. */
        headsetSetState(HEADSET_STATE_LIMBO);
        appPowerOffRequest();
    }
    else
    {
       /* Topology has stopped. Enable Wired audio controller. */
       HeadsetWiredAudioController_Enable();
    }
    return TRUE;
}

void headsetSmWiredAudioConnected(void)
{
    headsetState current_state = headsetGetState();

    DEBUG_LOG_INFO("headsetSmWiredAudioConnected Headset state %d",current_state);
    switch(current_state)
    {
        case HEADSET_STATE_POWERING_ON:
        case HEADSET_STATE_IDLE:
        case HEADSET_STATE_BUSY:
        {
          /* Move to Busy state */
            headsetSetState(HEADSET_STATE_BUSY);
        }
        break;

        default:
        break;
    }
}

void headsetSmWiredAudioDisconnected(void)
{
    headsetState current_state = headsetGetState();

    DEBUG_LOG_INFO("headsetSmWiredAudioDisconnected Headset state %d",current_state);
    switch(current_state)
    {
        case HEADSET_STATE_BUSY:
        {
            /* Move to headset idle state */
            headsetSetState(HEADSET_STATE_IDLE);
        }
        break;

        default:
        break;
    }
}

void headsetSmHandleMessage(Task task, MessageId id, Message message)
{
    UNUSED(task);

    if (isMessageUiInput(id))
    {
       headsetSmHandleUiInput(id);
       return;
    }

    switch (id)
    {
#ifdef INCLUDE_DFU
        case DFU_REQUESTED_TO_CONFIRM:
            /* Upgrade almost over, Stay in upgrade mode for upgrade to be completed */
            DEBUG_LOG_DEBUG("headsetSmHandleMessage APP_DFU_REQUESTED_TO_CONFIRM");
            Dfu_SetRebootReason(REBOOT_REASON_DFU_RESET);
            break;

        case DFU_REQUESTED_IN_PROGRESS:
            DEBUG_LOG_DEBUG("headsetSmHandleMessage APP_DFU_REQUESTED_IN_PROGRESS");
            Dfu_SetRebootReason(REBOOT_REASON_ABRUPT_RESET);
            break;

        case DFU_STARTED:
            DEBUG_LOG_DEBUG("headsetSmHandleMessage APP_DFU_STARTED");
            //Requesting QOS to be low latency over BLE connection at a cost of higher power consumption
            Dfu_RequestQOS();
            break;

        case DFU_COMPLETED:
            GattServerGatt_SetGattDbChanged();
            Dfu_SetRebootReason(REBOOT_REASON_NONE);
            DEBUG_LOG_DEBUG("headsetSmHandleMessage APP_DFU_COMPLETED");
            break;

        case DFU_ABORTED:
            DEBUG_LOG_DEBUG("headsetSmHandleMessage APP_DFU_ABORTED");
            //Release QOS which was earlier requested over BLE connection at a cost of higher power consumption
            Dfu_ReleaseQOS();
            break;

        case DFU_READY_FOR_SILENT_COMMIT:
            DEBUG_LOG_DEBUG("headsetSmHandleMessage APP_DFU_READY_FOR_SILENT_COMMIT");
            //Release QOS when Data transfer is completed for silent commit.
            Dfu_ReleaseQOS();
            break;
#endif
        case ANC_UPDATE_STATE_ENABLE_IND:
             headsetSmHandleAncUpdateStateEnableInd();
             DEBUG_LOG_DEBUG("headsetSmHandleMessage ANC_UPDATE_STATE_ENABLE_IND");
             break;

        case ANC_UPDATE_STATE_DISABLE_IND:
             headsetSmHandleAncUpdateStateDisableInd();
             DEBUG_LOG_DEBUG("headsetSmHandleMessage ANC_UPDATE_STATE_DISABLE_IND");
             break;

        case LEAKTHROUGH_UPDATE_STATE_IND:
             headsetSmHandleLeakthroughStateInd((LEAKTHROUGH_UPDATE_STATE_IND_T *)message);
             DEBUG_LOG_DEBUG("headsetSmHandleMessage LEAKTHROUGH_UPDATE_ENABLE_IND");
             break;

        case SYSTEM_STATE_STATE_CHANGE:
             headsetSmHandleSystemStateChange((SYSTEM_STATE_STATE_CHANGE_T *)message);
             break;

        /* Pairing completion confirmations */
        case PAIRING_PAIR_CFM:
            headsetSmHandlePairingPairConfirm((PAIRING_PAIR_CFM_T *)message);
            break;

        /*! Handset Service disconnected indication */
        case HANDSET_SERVICE_DISCONNECTED_IND:
             headsetSmHandleHandsetServiceDisconnectedInd((HANDSET_SERVICE_DISCONNECTED_IND_T *) message);
             break;

        /* Handset service connected indication */
        case HANDSET_SERVICE_CONNECTED_IND:
            headsetSmHandleHandsetServiceConnectedInd((HANDSET_SERVICE_CONNECTED_IND_T*)message);
            break;

        /* Topology Messages */
        case HEADSET_TOPOLOGY_STOP_CFM:
            headetSmHandleTopologyStopCfm(message);
            break;

        case HEADSET_TOPOLOGY_HANDSET_DISCONNECTED_IND:
            headsetSmHandleHeadsetTopologyHandsetDisInd((HEADSET_TOPOLOGY_HANDSET_DISCONNECTED_IND_T *)message);
            break;

        /* TODO Add handling for messages from various registered domains/components */

        /* AV status change indications */
        case AV_STREAMING_ACTIVE_IND:
        case AV_STREAMING_INACTIVE_IND:
        /* HFP status change indications */
        case APP_HFP_SCO_CONNECTED_IND:
        case APP_HFP_SCO_DISCONNECTED_IND:
            if(headsetSmIsActiveState(headsetGetState()))
            {
                headsetSetState(headsetSmDetermineCoreState());
            }
#ifdef INCLUDE_DFU
            /* Inform upgrade library when SCO is connected/disconnected */
            if (id == APP_HFP_SCO_CONNECTED_IND || id == APP_HFP_SCO_DISCONNECTED_IND)
            {
                UpgradeSetScoActive(id == APP_HFP_SCO_CONNECTED_IND);
            }
#endif
            break;

        /* Charger indications */
        case CHARGER_MESSAGE_DETACHED:
            headsetSmHandleChargerMessageDetached();
            break;
        case CHARGER_MESSAGE_CHARGING_OK:
        case CHARGER_MESSAGE_CHARGING_LOW:
            /* Consume frequently occuring charger messages with no operation required. */
            break;

        /* Power indications */
        case APP_POWER_SHUTDOWN_PREPARE_IND:
            DEBUG_LOG_DEBUG("headsetSmHandleMessage APP_POWER_SHUTDOWN_PREPARE_IND");
            headsetSmHandlePowerShutdownPrepareInd();
            break;

        case SM_INTERNAL_FACTORY_RESET:
            headsetSmHandleInternalFactoryReset();
            break;

        case SM_INTERNAL_PAIR_HANDSET:
            headsetSmHandleInternalPairHandset();
            break;

        case SM_INTERNAL_DELETE_HANDSETS:
            headsetSmHandleInternalDeleteHandsets();
            break;

        case SM_INTERNAL_POWER_OFF:
            headsetSmHandleInternalPowerOff();
            break;

        case SM_INTERNAL_TIMEOUT_IDLE:
            headsetSmHandleTimeoutIdle();
            break;

        case SM_INTERNAL_TIMEOUT_LIMBO:
            headsetSmHandleTimeoutLimbo();
            break;

        case SM_INTERNAL_LINK_DISCONNECTION_COMPLETE:
            headsetSmHandleInternalLinkDisconnectionComplete();
            break;

        case SM_INTERNAL_TIMEOUT_LINK_DISCONNECTION:
            headsetSmLinkDisconnectionTimeout();
            break;

        default:
            UnexpectedMessage_HandleMessage(id);
            break;
    }
}
/*! \brief  Provides Headset active state context to the User Interface module.Serves as a callback to the
 *          UI for current context. Returns only context_app_sm_active / context_app_sm_inactive.

    \param[in]  void

    \return     current_sm_ctxt - current context of sm module.
*/
static unsigned headsetSm_GetApplicationActiveStateContext(void)
{
    sm_provider_context_t context = context_app_sm_active;

    if(headsetSm_GetApplicationCurrentContext() == context_app_sm_powered_off)
    {
        context = context_app_sm_inactive;
    }

    return (unsigned)context;
}

/*! \brief Initiate disconnect of handset link */
bool headsetSmDisconnectLink(void)
{
    bdaddr handset_addr = {0};
    bool disconnecting = FALSE;

    if (appDeviceGetHandsetBdAddr(&handset_addr) && appDeviceIsHandsetConnected())
    {
        HeadsetTopology_DisconnectAllHandsets();
        disconnecting = TRUE;
    }

    return disconnecting;
}

/*! \brief Initialise the main application state machine.
 */
bool headsetSmInit(Task init_task)
{
    smTaskData* sm = SmGetTaskData();
    memset(sm, 0, sizeof(*sm));
    sm->task.handler = headsetSmHandleMessage;
    sm->state = HEADSET_STATE_NULL;
    sm->disconnect_lock= 0;
    sm->user_pairing = FALSE;
    sm->auto_poweron = FALSE;
    if( (SystemReboot_GetAction() == reboot_action_active_state) || headsetSmIsLastResetDueToPanic() )
    {
        headsetSmSetAutoPowerOn();
        SystemReboot_ResetAction();
    }
    /* register with connection manager to get notification of (dis)connections */
    ConManagerRegisterConnectionsClient(&sm->task);

    /* register with HFP for changes in state */
    HfpProfile_RegisterStatusClient(&sm->task);

    /* register with AV to receive notifications of A2DP and AVRCP activity */
    appAvStatusClientRegister(&sm->task);

    /* register with power to receive sleep/shutdown messages. */
    appPowerClientRegister(&sm->task);

    /* register with charger monitor to receive charger messages. */    
    (void)Charger_ClientRegister(&sm->task);

    /* register with handset service as we need disconnect and connect notification */
    HandsetService_ClientRegister(&sm->task);

    /* Register for topology message indications */
    HeadsetTopology_RegisterMessageClient(headsetSmGetTask());

    /* Register for connection manager TP message indication */
    ConManagerRegisterTpConnectionsObserver(cm_transport_bredr, headsetSmGetTask());
    
    /* Register for system state change indications */
    SystemState_RegisterForStateChanges(&sm->task);

    Ui_RegisterUiInputConsumer(headsetSmGetTask(), sm_ui_inputs, ARRAY_DIM(sm_ui_inputs));

    /* Register sm as ui provider*/
    Ui_RegisterUiProvider(ui_provider_app_sm, headsetSm_GetApplicationActiveStateContext );

    /* Register with ANC state manager to receive ANC ON/OFF notifications */
    AncStateManager_ClientRegister(&sm->task);

    /* Register to receive Leakthrough Enable/Disable notifications */
    AecLeakthrough_ClientRegister(&sm->task);

    /* If DFU support is enabled, then set the QoS as low latency for better
     * DFU performance over LE Transport.
     * This will come at the cost of high power consumption.
     */

    UNUSED(init_task);
    return TRUE;
}

