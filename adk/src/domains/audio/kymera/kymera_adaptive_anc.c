/*!
\copyright  Copyright (c) 2020 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version    
\file       kymera_adaptive_anc.c
\brief      Kymera Adaptive ANC code
*/


#include <vmal.h>
#include <file.h>
#include <stdlib.h>
#include <cap_id_prim.h>

#include "kymera_adaptive_anc.h"
#include "kymera_config.h"
#include "anc_state_manager.h"
#include "microphones.h"
#include "kymera_aec.h"
#include "kymera_va.h"
#include "kymera_private.h"
#include "kymera_mic_if.h"

#ifdef ENABLE_ADAPTIVE_ANC

#ifdef INCLUDE_MIC_CONCURRENCY
#define MAX_CHAIN (3)
#define CHAIN_MIC_REF_PATH_SPLITTER (MAX_CHAIN-1)
#define CHAIN_FBC (CHAIN_MIC_REF_PATH_SPLITTER-1)
#define CHAIN_AANC (CHAIN_FBC-1)
#else
#define MAX_CHAIN (5)
#define CHAIN_TX_RESAMPLER (MAX_CHAIN-1)
#define CHAIN_TX_SPLITTER (CHAIN_TX_RESAMPLER-1)
#define CHAIN_RX_RESAMPLER (CHAIN_TX_SPLITTER-1)
#define CHAIN_RX_SPLITTER (CHAIN_RX_RESAMPLER-1)
#define CHAIN_AANC (CHAIN_RX_SPLITTER-1)
#endif

#define AANC_SAMPLE_RATE 16000
#define MAX_AANC_MICS (2)

#define kymeraAdaptiveAnc_IsAancActive() (kymeraAdaptiveAnc_GetChain(CHAIN_AANC) != NULL)
#define kymeraAdaptiveAnc_PanicIfNotActive()  if(!kymeraAdaptiveAnc_IsAancActive()) \
                                                                                    Panic()
#ifdef INCLUDE_MIC_CONCURRENCY
#define kymeraAdaptiveAnc_IsAancFbcActive() (kymeraAdaptiveAnc_GetChain(CHAIN_FBC) != NULL)
#define kymeraAdaptiveAnc_IsSplitterInMicRefPathCreated() (kymeraAdaptiveAnc_GetChain(CHAIN_MIC_REF_PATH_SPLITTER) != NULL)
#else
#define kymeraAdaptiveAnc_IsRxSplitterCreated() (kymeraAdaptiveAnc_GetChain(CHAIN_RX_SPLITTER) != NULL)
#define kymeraAdaptiveAnc_IsRxResamplerCreated() (kymeraAdaptiveAnc_GetChain(CHAIN_RX_RESAMPLER) != NULL)
#define kymeraAdaptiveAnc_IsTxResamplerCreated() (kymeraAdaptiveAnc_GetChain(CHAIN_TX_RESAMPLER) != NULL)
#define kymeraAdaptiveAnc_IsTxSplitterCreated() (kymeraAdaptiveAnc_GetChain(CHAIN_TX_SPLITTER) != NULL)
#endif

#define CONVERT_MSEC_TO_SEC(value) (value/1000)

#define IN_EAR  TRUE
#define OUT_OF_EAR !IN_EAR
#define ENABLE_ADAPTIVITY TRUE
#define DISABLE_ADAPTIVITY !ENABLE_ADAPTIVITY

#define NUM_STATUS_VAR              24
#define CUR_MODE_STATUS_OFFSET       0
#define FLAGS_STATUS_OFFSET          7
#define FF_GAIN_STATUS_OFFSET        8
#define FLAG_POS_QUIET_MODE         20

#define BIT_MASK(FLAG_POS)          (1 << FLAG_POS)

#define AANC_TUNNING_START_DELAY    (200U)

/*! Macro for creating messages */
#define MAKE_KYMERA_MESSAGE(TYPE) \
    TYPE##_T *message = PanicUnlessNew(TYPE##_T);

#define AANC_TUNING_SINK_USB        0
#define AANC_TUNING_SINK_UNUSED     1 /* Unused */
#define AANC_TUNING_SINK_INT_MIC    2 /* Internal Mic In */
#define AANC_TUNING_SINK_EXT_MIC    3 /* External Mic In */

#define AANC_TUNING_SOURCE_DAC      0
#define AANC_TUNING_SOURCE_UNUSED   1 /* Unused */
#define AANC_TUNING_SOURCE_INT_MIC  2 /* Forwards Int Mic */
#define AANC_TUNING_SOURCE_EXT_MIC  3 /* Forwards Ext Mic */

#ifdef DOWNLOAD_USB_AUDIO
#define EB_CAP_ID_USB_AUDIO_RX CAP_ID_DOWNLOAD_USB_AUDIO_RX
#define EB_CAP_ID_USB_AUDIO_TX CAP_ID_DOWNLOAD_USB_AUDIO_TX
#else
#define EB_CAP_ID_USB_AUDIO_RX CAP_ID_USB_AUDIO_RX
#define EB_CAP_ID_USB_AUDIO_TX CAP_ID_USB_AUDIO_TX
#endif

/* AANC Capability ID*/
/* Hard coded to 0x409F to fix unity. Will be modified*/
#define CAP_ID_DOWNLOAD_AANC_TUNING 0x409F

static kymera_chain_handle_t adaptive_anc_chains[MAX_CHAIN] = {0};

static void kymeraAdaptiveAnc_UpdateAdaptivity(bool enable_adaptivity);

static kymera_chain_handle_t kymeraAdaptiveAnc_GetChain(uint8 index)
{
    return ((index < MAX_CHAIN) ? adaptive_anc_chains[index] : NULL);
}

static void kymeraAdaptiveAnc_SetChain(uint8 index, kymera_chain_handle_t chain)
{
    if(index < MAX_CHAIN)
        adaptive_anc_chains[index] = chain;
}

static Source kymeraAdaptiveAnc_GetOutput(uint8 index, chain_endpoint_role_t output_role)
{
    return ChainGetOutput(kymeraAdaptiveAnc_GetChain(index), output_role);
}

static Sink kymeraAdaptiveAnc_GetInput(uint8 index, chain_endpoint_role_t output_role)
{
    return ChainGetInput(kymeraAdaptiveAnc_GetChain(index), output_role);
}

#ifdef INCLUDE_MIC_CONCURRENCY

/* Mic interface callback functions */
static bool kymeraAdaptiveAnc_MicGetConnectionParameters(microphone_number_t *mic_ids, Sink *mic_sinks, uint8 *num_of_mics, uint32 *sample_rate, Sink *aec_ref_sink);
static bool kymeraAdaptiveAnc_MicDisconnectIndication(const mic_change_info_t *info);
static void kymeraAdaptiveAnc_MicReadyForReconnectionIndication(const mic_change_info_t *info);
static void kymeraAdaptiveAnc_MicReconnectedIndication(void);
static void kymeraAdaptiveAnc_MicUserUpdatedStateIndication(void);
static void kymeraAdaptiveAnc_MicUserChangePendingNotification(const mic_change_info_t *info);
/* AANC support functions */
static bool kymeraAdapativeAnc_IsConcurrencyAudioSourceActive(void);
static bool kymeraAdapativeAnc_IsMusicActive(void);
static bool kymeraAdaptiveAnc_IsConcurrentUser(mic_users_t active_user);
static bool kymeraAdaptiveAnc_IsVaSessionActive(void);
static void kymeraAdaptiveAnc_CreateEchoCancellerChain(void);
static void kymeraAdaptiveAnc_ConnectConcurrency(void);
static void kymeraAdaptiveAnc_StartConcurrency(bool transition);
static void kymeraAdaptiveAnc_StopConcurrency(void);
static void kymeraAdaptiveAnc_DisconnectConcurrency(void);
static void kymeraAdaptiveAnc_DestroyConcurrency(bool transition);
static bool kymeraAdapativeAnc_IsVoiceActive(void);

static microphone_number_t kymera_AdaptiveAncMics[MAX_AANC_MICS] =
{
    appConfigAncFeedForwardMic(),
    appConfigAncFeedBackMic()
};

static const mic_callbacks_t kymera_AdaptiveAncCallbacks =
{
    .MicGetConnectionParameters = kymeraAdaptiveAnc_MicGetConnectionParameters,
    .MicDisconnectIndication = kymeraAdaptiveAnc_MicDisconnectIndication,
    .MicReadyForReconnectionIndication = kymeraAdaptiveAnc_MicReadyForReconnectionIndication,
    .MicReconnectedIndication = kymeraAdaptiveAnc_MicReconnectedIndication,
    .MicUserUpdatedState = kymeraAdaptiveAnc_MicUserUpdatedStateIndication,
    .MicUserChangePendingNotification = kymeraAdaptiveAnc_MicUserChangePendingNotification
};

static mic_user_state_t kymera_AancMicState = mic_user_state_always_interrupt;

static const mic_registry_per_user_t kymera_AdaptiveAncRegistry =
{
    .user = mic_user_aanc,
    .callbacks = &kymera_AdaptiveAncCallbacks,
    .mandatory_mic_ids = &kymera_AdaptiveAncMics[0],
    .num_of_mandatory_mics = MAX_AANC_MICS,
    .mic_user_state = &kymera_AancMicState,
};


/*! For a reconnection the mic parameters are sent to the mic interface.
 *  return TRUE to reconnect with the given parameters
 */
static bool kymeraAdaptiveAnc_MicGetConnectionParameters(microphone_number_t *mic_ids, Sink *mic_sinks, uint8 *num_of_mics, uint32 *sample_rate, Sink *aec_ref_sink)
{
    *sample_rate = AANC_SAMPLE_RATE;
    *num_of_mics = MAX_AANC_MICS;
    mic_ids[0] = appConfigAncFeedForwardMic();
    mic_ids[1] = appConfigAncFeedBackMic();

    DEBUG_LOG("kymeraAdaptiveAnc_MicGetConnectionParameters");

    if (kymeraAdaptiveAnc_IsAancFbcActive())
    {
        DEBUG_LOG("AANC concurrency mic sinks");
        mic_sinks[0] = kymeraAdaptiveAnc_GetInput(CHAIN_FBC, EPR_AANC_FBC_FF_MIC_IN);
        mic_sinks[1] = kymeraAdaptiveAnc_GetInput(CHAIN_FBC, EPR_AANC_FBC_ERR_MIC_IN);
        aec_ref_sink[0] = kymeraAdaptiveAnc_GetInput(CHAIN_MIC_REF_PATH_SPLITTER, EPR_SPLT_MIC_REF_IN);
    }
    else
    {    
        DEBUG_LOG("AANC standalone mic sinks");
        mic_sinks[0] = kymeraAdaptiveAnc_GetInput(CHAIN_AANC, EPR_AANC_FF_MIC_IN);
        mic_sinks[1] = kymeraAdaptiveAnc_GetInput(CHAIN_AANC, EPR_AANC_ERR_MIC_IN);
        aec_ref_sink[0] = NULL;
    }

    return TRUE;
}

/*! When any user is connecting or disconnecting, an indication is sent from mic framework in advance of disconnection.
 *  With this indication the state of a user can be changed to prevent / allow disconnection and then reconnection.
 */
static void kymeraAdaptiveAnc_MicUserChangePendingNotification(const mic_change_info_t *info)
{
    DEBUG_LOG("kymeraAdaptiveAnc_MicUserChangePendingNotification user %d, event %d", info->user, info->event);

    kymera_AancMicState = mic_user_state_always_interrupt;
    
    if ((info->event & mic_event_connecting)==mic_event_connecting)
    {
        if (!kymeraAdaptiveAnc_IsConcurrentUser(info->user))
        {        
            DEBUG_LOG("mic_user_state_interruptible");
            kymera_AancMicState = mic_user_state_interruptible;
        }
    }
  
    Kymera_MicUserUpdatedState(mic_user_aanc);
}


/*! Before the microphones are disconnected, all users get informed with a DisconnectIndication.
 * return FALSE: accept disconnection
 * return TRUE: Try to reconnect the microphones. This will trigger a kymeraAdaptiveAnc_MicGetConnectionParameters 
 */
static bool kymeraAdaptiveAnc_MicDisconnectIndication(const mic_change_info_t *info)
{
    DEBUG_LOG("kymeraAdaptiveAnc_MicDisconnectIndication user %d, event %d", info->user, info->event);

    if (kymeraAdaptiveAnc_IsAancActive())
    {
        if ((info->event & mic_event_disconnecting)==mic_event_disconnecting) /*SCO/VA disconnecting, AANC to go back to standalone*/
        {
            DEBUG_LOG("kymeraAdaptiveAnc_MicDisconnectIndication due to concurrency audio source disconnected");
            kymeraAdaptiveAnc_StopConcurrency();
            kymeraAdaptiveAnc_DisconnectConcurrency();

            if(kymeraAdaptiveAnc_IsConcurrentUser(info->user))
            {
                if(!kymeraAdapativeAnc_IsMusicActive())
                {
                    kymeraAdaptiveAnc_DestroyConcurrency(TRUE);
                }
            }
        }
        else if ((info->event & mic_event_connecting)==mic_event_connecting) /*SCO/VA connecting while AANC enabled*/
        {
            DEBUG_LOG("kymeraAdaptiveAnc_MicDisconnectIndication due to concurrency audio source connected");
            kymeraAdaptiveAnc_StopConcurrency();
            
            if(kymeraAdapativeAnc_IsMusicActive())
            {
                kymeraAdaptiveAnc_DisconnectConcurrency();
            }
        }
        return TRUE;
    }

    return FALSE;
}


/*! When all users are disconnected, the users are informed with the ReadyForReconnection Indication */
static void kymeraAdaptiveAnc_MicReadyForReconnectionIndication(const mic_change_info_t *info)
{
    DEBUG_LOG("kymeraAdaptiveAnc_MicReadyForReconnectionIndication user %d, event %d", info->user, info->event);

    /*When both AANC is enabled and SCO is active, MicDisconnectIndication can happen during MicConnect and MicDisconnect
       If AANC is enabled and then SCO started, MicConnect will induce a MicDisconnectIndication at ANC (returning TRUE for reconnect) 
       and then a MicReadyForReconnectionIndication which will configure Mics with FBC mic sinks (GetConnectionParameters returning FBC sinks based on state). 
       This is moving to concurrency.*/

    if (kymeraAdaptiveAnc_IsAancActive())
    {    
        if (((info->event & mic_event_connecting)==mic_event_connecting) && kymeraAdaptiveAnc_IsConcurrentUser(info->user))
        {
            DEBUG_LOG("Moving to concurrency");
            /* Boost DSP clock to turbo */
            appKymeraSetActiveDspClock(AUDIO_DSP_TURBO_CLOCK);
            kymeraAdaptiveAnc_CreateEchoCancellerChain();
        }
    }
}

/*! This indication is sent if the microphones have been reconnected after a DisconnectIndication.
 * Also used in cases where ANC is enabled and SCO is started, then a kymeraAdaptiveAnc_MicDisconnectIndication
 * and a kymeraAdaptiveAnc_MicReconnectedIndication is called, which will configure the new mic sinks through 
 * kymeraAdaptiveAnc_MicGetConnectionParameters callback from mic framework
 */
static void kymeraAdaptiveAnc_MicReconnectedIndication(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_MicReconnectedIndication");

    appKymeraConfigureDspPowerMode();

    if (kymeraAdaptiveAnc_IsAancActive())
    {
        if(kymeraAdapativeAnc_IsConcurrencyAudioSourceActive())
        {
            kymeraAdaptiveAnc_ConnectConcurrency();
            kymeraAdaptiveAnc_StartConcurrency(TRUE);
            if(kymeraAdaptiveAnc_IsVaSessionActive() && !kymeraAdapativeAnc_IsMusicActive())
                appKymeraSetState(KYMERA_STATE_ADAPTIVE_ANC_STARTED);
            appKymeraSetConcurrentState(KYMERA_CON_STATE_WITH_ADAPTIVE_ANC);
            /* Update optimum DSP clock for AANC usecase */
            appKymeraConfigureDspPowerMode();
            DEBUG_LOG("kymeraAdaptiveAnc_MicReconnectedIndication, concurrency active");
        }
        else
        {
            kymeraAdaptiveAnc_DestroyConcurrency(TRUE);
            kymeraAdaptiveAnc_UpdateAdaptivity(ENABLE_ADAPTIVITY);
            appKymeraSetState(KYMERA_STATE_ADAPTIVE_ANC_STARTED);
            appKymeraSetConcurrentState(KYMERA_CON_STATE_NONE);
            /* Update optimum DSP clock for AANC usecase */
            appKymeraConfigureDspPowerMode();
            DEBUG_LOG("kymeraAdaptiveAnc_MicReconnectedIndication, standalone active");
        }
    }
}

static void kymeraAdaptiveAnc_MicUserUpdatedStateIndication(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_MicUserUpdatedStateIndication");
}

#endif

/*!
 *  Registers AANC callbacks in the mic interface layer
 */
void KymeraAdaptiveAnc_Init(void)
{
#ifdef INCLUDE_MIC_CONCURRENCY
    Kymera_MicRegisterUser(&kymera_AdaptiveAncRegistry);
#endif
}

/**************** Utility functions for Adaptive ANC chain ********************************/
/*Reads the static gain for current mode in library*/
static void kymeraAdaptiveAnc_SetStaticGain(Operator op, audio_anc_path_id feedforward_anc_path, adaptive_anc_hw_channel_t hw_channel)
{
    uint16 ff_coarse_static_gain;
    uint16 fb_coarse_static_gain;
    uint16 ec_coarse_static_gain;
    uint8 ff_fine_static_gain;
    uint8 fb_fine_static_gain;
    uint8 ec_fine_static_gain;

    audio_anc_instance inst = hw_channel ? AUDIO_ANC_INSTANCE_1 : AUDIO_ANC_INSTANCE_0;

    /*If hybrid is configured, feedforward path is AUDIO_ANC_PATH_ID_FFB and feedback path will be AUDIO_ANC_PATH_ID_FFA*/
    audio_anc_path_id feedback_anc_path = (feedforward_anc_path==AUDIO_ANC_PATH_ID_FFB)?(AUDIO_ANC_PATH_ID_FFA):(AUDIO_ANC_PATH_ID_FFB);/*TBD for Feed forward ANC mode*/

     /*Update gains*/
    AncReadCoarseGainFromInstance(inst, feedforward_anc_path, &ff_coarse_static_gain);
    AncReadFineGainFromInstance(inst, feedforward_anc_path, &ff_fine_static_gain);
     
    AncReadCoarseGainFromInstance(inst, feedback_anc_path, &fb_coarse_static_gain);
    AncReadFineGainFromInstance(inst, feedback_anc_path, &fb_fine_static_gain);

    AncReadCoarseGainFromInstance(inst, AUDIO_ANC_PATH_ID_FB, &ec_coarse_static_gain);
    AncReadFineGainFromInstance(inst, AUDIO_ANC_PATH_ID_FB, &ec_fine_static_gain);

    OperatorsAdaptiveAncSetStaticGain(op, ff_coarse_static_gain, ff_fine_static_gain, fb_coarse_static_gain, fb_fine_static_gain, ec_coarse_static_gain, ec_fine_static_gain);
}

static void kymeraAdaptiveAnc_SetControlModelForParallelTopology(Operator op,audio_anc_path_id control_path,adaptive_anc_coefficients_t *numerator, adaptive_anc_coefficients_t *denominator)
{
    /*ANC library to read the control coefficients */
    AncReadModelCoefficients(AUDIO_ANC_INSTANCE_0, control_path, (uint32*)denominator->coefficients, (uint32*)numerator->coefficients);
    OperatorsParallelAdaptiveAncSetControlModel(op,AUDIO_ANC_INSTANCE_0,numerator,denominator);
    AncReadModelCoefficients(AUDIO_ANC_INSTANCE_1, control_path, (uint32*)denominator->coefficients, (uint32*)numerator->coefficients);
    OperatorsParallelAdaptiveAncSetControlModel(op,AUDIO_ANC_INSTANCE_1,numerator,denominator);
}

static void kymeraAdaptiveAnc_SetControlModelForSingleTopology(Operator op,audio_anc_instance inst,audio_anc_path_id control_path,adaptive_anc_coefficients_t *numerator, adaptive_anc_coefficients_t *denominator)
{
    /*ANC library to read the control coefficients */
    AncReadModelCoefficients(inst, control_path, (uint32*)denominator->coefficients, (uint32*)numerator->coefficients);
    OperatorsAdaptiveAncSetControlModel(op, numerator, denominator);
}

static void kymeraAdaptiveAnc_SetControlPlantModel(Operator op, audio_anc_path_id control_path, adaptive_anc_hw_channel_t hw_channel)
{
#define NUM_DENOMINATORS_COEFFICIENTS 7
#define NUM_NUMERATORS_COEFFICIENTS 8

    /* Currently number of numerators & denominators are defaulted to QCC514XX specific value.
        However this might change for other family of chipset. So, ideally this shall be supplied from
        ANC library during the coarse of time */
    uint8 num_denominators = NUM_DENOMINATORS_COEFFICIENTS;
    uint8 num_numerators = NUM_NUMERATORS_COEFFICIENTS;
    adaptive_anc_coefficients_t *denominator, *numerator;
    kymeraTaskData *theKymera = KymeraGetTaskData();


    audio_anc_instance inst = (hw_channel) ? AUDIO_ANC_INSTANCE_1 : AUDIO_ANC_INSTANCE_0;

    /* Regsiter a listener with the AANC*/
    MessageOperatorTask(op, &theKymera->task);

    /*If hybrid is configured, feedforward path is AUDIO_ANC_PATH_ID_FFB and feedback path will be AUDIO_ANC_PATH_ID_FFA*/
    /*audio_anc_path_id fb_path = (param->control_path==AUDIO_ANC_PATH_ID_FFB)?(AUDIO_ANC_PATH_ID_FFA):(AUDIO_ANC_PATH_ID_FFB); */

    denominator = OperatorsCreateAdaptiveAncCoefficientsData(num_denominators);
    numerator= OperatorsCreateAdaptiveAncCoefficientsData(num_numerators);

    if(appKymeraIsParallelAncFilterEnabled())
    {
        kymeraAdaptiveAnc_SetControlModelForParallelTopology(op,control_path,numerator,denominator);
    }
    else
    {
        kymeraAdaptiveAnc_SetControlModelForSingleTopology(op,inst,control_path,numerator,denominator);
    }

    /* Free it, so that it can be re-used */
    free(denominator);
    free(numerator);

    denominator = OperatorsCreateAdaptiveAncCoefficientsData(num_denominators);
    numerator= OperatorsCreateAdaptiveAncCoefficientsData(num_numerators);
    /*ANC library to read the plant coefficients */
    AncReadModelCoefficients(inst, AUDIO_ANC_PATH_ID_FB, (uint32*)denominator->coefficients, (uint32*)numerator->coefficients);
    OperatorsAdaptiveAncSetPlantModel(op, numerator, denominator);
    /* Free it, so that it can be re-used */
    free(denominator);
    free(numerator);
}

static void kymeraAdaptiveAnc_SetParallelTopology(Operator op)
{
    OperatorsAdaptiveAncSetParallelTopology(op,adaptive_anc_filter_config_parallel_topology);
}

static void kymeraAdaptiveAnc_ConfigureAancChain(Operator op, const KYMERA_INTERNAL_AANC_ENABLE_T* param, aanc_usecase_t usecase)
{

    if(appKymeraIsParallelAncFilterEnabled())
    {
        kymeraAdaptiveAnc_SetParallelTopology(op);
    }

    kymeraAdaptiveAnc_SetControlPlantModel(op, param->control_path, param->hw_channel);
    kymeraAdaptiveAnc_SetStaticGain(op, param->control_path, param->hw_channel);
    OperatorsAdaptiveAncSetHwChannelCtrl(op, param->hw_channel);
    OperatorsAdaptiveAncSetInEarCtrl(op, param->in_ear);
    OperatorsStandardSetUCID(op, param->current_mode);
    
    switch (usecase)
    {
        case aanc_usecase_standalone:
            if (param->current_mode == anc_mode_1)
            {
                OperatorsAdaptiveAncSetModeOverrideCtrl(op, adaptive_anc_mode_full);
                DEBUG_LOG("AANC comes up in Full Proc");
            }
            else
            {       
                OperatorsAdaptiveAncSetModeOverrideCtrl(op, adaptive_anc_mode_static);
                DEBUG_LOG("AANC comes up in Static");
            }
            break;

        case aanc_usecase_sco_concurrency:
            OperatorsAdaptiveAncSetModeOverrideCtrl(op, adaptive_anc_mode_standby);
            DEBUG_LOG("AANC comes up in Standby");
            break;

        default:
            DEBUG_LOG("NOTE: AANC is in default mode of capability");
            break;
    }
}

static void kymeraAdaptiveAnc_UpdateInEarStatus(bool in_ear)
{
    Operator op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_AANC), OPR_AANC);
    OperatorsAdaptiveAncSetInEarCtrl(op, in_ear);
}

static void kymeraAdaptiveAnc_UpdateAdaptivity(bool enable_adaptivity)
{
    Operator op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_AANC), OPR_AANC);
    if(enable_adaptivity)
        OperatorsAdaptiveAncEnableGainCalculation(op);
    else
        OperatorsAdaptiveAncDisableGainCalculation(op);
}

static void kymeraAdaptiveAnc_CreateAancChain(const KYMERA_INTERNAL_AANC_ENABLE_T* param, aanc_usecase_t usecase)
{
    DEBUG_LOG("kymeraAdaptiveAnc_CreateAancChain");
    PanicNotNull(kymeraAdaptiveAnc_GetChain(CHAIN_AANC));
    kymeraAdaptiveAnc_SetChain(CHAIN_AANC, PanicNull(ChainCreate(Kymera_GetChainConfigs()->chain_aanc_config)));

    Operator op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_AANC), OPR_AANC);
    kymeraAdaptiveAnc_ConfigureAancChain(op, param, usecase);
    ChainConnect(kymeraAdaptiveAnc_GetChain(CHAIN_AANC));
}

static void kymeraAdaptiveAnc_DestroyAancChain(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_DestroyAancChain");
    PanicNull(kymeraAdaptiveAnc_GetChain(CHAIN_AANC));
    ChainDestroy(kymeraAdaptiveAnc_GetChain(CHAIN_AANC));
    kymeraAdaptiveAnc_SetChain(CHAIN_AANC, NULL);
}
static get_status_data_t* kymeraAdaptiveAnc_GetStatusData(void)
{
    Operator op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_AANC), OPR_AANC);
    get_status_data_t* get_status = OperatorsCreateGetStatusData(NUM_STATUS_VAR);
    OperatorsAdaptiveAncGetStatus(op, get_status);
    return get_status;
}

static void kymeraAdaptiveAnc_GetStatusFlags(uint32 *flags)
{
    get_status_data_t* get_status = kymeraAdaptiveAnc_GetStatusData();
    *flags = (uint32)(get_status->value[FLAGS_STATUS_OFFSET]);
    free(get_status);
}

static void kymeraAdaptiveAnc_GetGain(uint8 *warm_gain)
{
    get_status_data_t* get_status = kymeraAdaptiveAnc_GetStatusData();
    *warm_gain = (uint8)(get_status->value[FF_GAIN_STATUS_OFFSET]);
    free(get_status);
}

static void kymeraAdaptiveAnc_GetCurrentMode(adaptive_anc_mode_t *aanc_mode)
{
    get_status_data_t* get_status = kymeraAdaptiveAnc_GetStatusData();
    *aanc_mode = (adaptive_anc_mode_t)(get_status->value[CUR_MODE_STATUS_OFFSET]);
    free(get_status);
}

static bool kymeraAdaptiveAnc_IsNoiseLevelBelowQuietModeThreshold(void)
{
    uint32 flags;
    kymeraAdaptiveAnc_GetStatusFlags(&flags);
    return (flags & BIT_MASK(FLAG_POS_QUIET_MODE));
}

static void kymeraAdaptiveAnc_SetGainValues(uint32 mantissa_val, uint32 exponent_val)
{
    Operator op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_AANC), OPR_AANC);
    OperatorsAdaptiveAncSetGainParameters(op, mantissa_val, exponent_val);
}


#ifndef INCLUDE_MIC_CONCURRENCY
static Source kymeraAdaptiveAnc_GetOutputForRole(chain_endpoint_role_t role)
{
    Source src = NULL;
    switch(role)
    {
        case EPR_SPLT_AEC_OUT: /* fall-thru */
        case EPR_SPLT_OUT_ANC_VAD:
            src =kymeraAdaptiveAnc_GetOutput(CHAIN_RX_SPLITTER, role);
            break;

        case EPR_SPLT_OUT_CVC_SEND:
            src =kymeraAdaptiveAnc_GetOutput(CHAIN_TX_SPLITTER, role);
            break;

        case EPR_IIR_8K_OUT1: /* fall-thru */
        case EPR_IIR_8K_OUT2:
        case EPR_IIR_AEC_REF_8K_OUT:
            src =kymeraAdaptiveAnc_GetOutput(CHAIN_TX_RESAMPLER, role);
            break;
            
        /* TODO: Add adaptive ANC role if required */
        default:
            src = NULL;
            break;
    }
    return src;
}

/**************** Utility functions for AANC Splitter chain ********************************/
static void kymeraAdaptiveAnc_ConfigureRxSplitterChain(void)
{
    Operator op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_RX_SPLITTER), OPR_AANC_SPLT_SCO_RX);
    OperatorsSplitterSetWorkingMode(op, splitter_mode_clone_input);
    OperatorsSplitterEnableSecondOutput(op, FALSE);
    OperatorsSplitterSetDataFormat(op, operator_data_format_pcm);
}

static void kymeraAdaptiveAnc_CreateRxSplitterChain(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_CreateRxSplitterChain");
    PanicNotNull(kymeraAdaptiveAnc_GetChain(CHAIN_RX_SPLITTER));
    kymeraAdaptiveAnc_SetChain(CHAIN_RX_SPLITTER, PanicNull(ChainCreate(Kymera_GetChainConfigs()->chain_aanc_splitter_sco_rx_path_config)));
    kymeraAdaptiveAnc_ConfigureRxSplitterChain();
    ChainConnect(kymeraAdaptiveAnc_GetChain(CHAIN_RX_SPLITTER));
}

static void kymeraAdaptiveAnc_DestroyRxSplitterChain(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_DestroyRxSplitterChain");
    PanicNull(kymeraAdaptiveAnc_GetChain(CHAIN_RX_SPLITTER));
    ChainStop(kymeraAdaptiveAnc_GetChain(CHAIN_RX_SPLITTER));
    ChainDestroy(kymeraAdaptiveAnc_GetChain(CHAIN_RX_SPLITTER));
    kymeraAdaptiveAnc_SetChain(CHAIN_RX_SPLITTER, NULL);
}

/**************** Utility functions for AANC Rx Resampler chain ********************************/
static void kymeraAdaptiveAnc_ConfigureRxResamplerChain(void)
{
    Operator op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_RX_RESAMPLER), OPR_AANC_UP_SAMPLE);
    OperatorsResamplerSetConversionRate(op, 8000, AANC_SAMPLE_RATE);
}

static void kymeraAdaptiveAnc_CreateRxResamplerChain(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_CreateRxResamplerChain");
    PanicNotNull(kymeraAdaptiveAnc_GetChain(CHAIN_RX_RESAMPLER));
    kymeraAdaptiveAnc_SetChain(CHAIN_RX_RESAMPLER, PanicNull(ChainCreate(Kymera_GetChainConfigs()->chain_aanc_resampler_sco_rx_path_config)));
    kymeraAdaptiveAnc_ConfigureRxResamplerChain();
    ChainConnect(kymeraAdaptiveAnc_GetChain(CHAIN_RX_RESAMPLER));
}

static void kymeraAdaptiveAnc_DestroyRxResamplerChain(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_DestroyRxResamplerChain");
    PanicNull(kymeraAdaptiveAnc_GetChain(CHAIN_RX_RESAMPLER));
    ChainStop(kymeraAdaptiveAnc_GetChain(CHAIN_RX_RESAMPLER));
    ChainDestroy(kymeraAdaptiveAnc_GetChain(CHAIN_RX_RESAMPLER));
    kymeraAdaptiveAnc_SetChain(CHAIN_RX_RESAMPLER, NULL);
}

/*!Connect SCO Rx from common output chain to Adaptive ANC splitter
*/
static void kymeraAdaptiveAnc_ConnectScoReceive(Source sco_rx, appKymeraScoMode mode)
{    
    if (mode == SCO_NB)
    {
        kymeraAdaptiveAnc_CreateRxResamplerChain();
        kymeraAdaptiveAnc_CreateRxSplitterChain();
        /* Connect SCO Rx to Rx Resampler input */
        PanicNull(StreamConnect(sco_rx, kymeraAdaptiveAnc_GetInput(CHAIN_RX_RESAMPLER, EPR_IIR_RX_8K_IN1)));
        /* Connect Rx Resampler to splitter input */
        PanicNull(StreamConnect(kymeraAdaptiveAnc_GetOutput(CHAIN_RX_RESAMPLER, EPR_IIR_RX_16K_OUT1), kymeraAdaptiveAnc_GetInput(CHAIN_RX_SPLITTER, EPR_SPLT_SCO_IN)));
    }
    else
    {
        kymeraAdaptiveAnc_CreateRxSplitterChain();
        /* Connect SCO Rx to splitter input */
        PanicNull(StreamConnect(sco_rx, kymeraAdaptiveAnc_GetInput(CHAIN_RX_SPLITTER, EPR_SPLT_SCO_IN)));
    }
}

static void kymeraAdaptiveAnc_DeactivateRxSplitterSecondOutput(void)
{
    if(kymeraAdaptiveAnc_IsRxSplitterCreated())
    {    
        Operator splitter_op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_RX_SPLITTER), OPR_AANC_SPLT_SCO_RX);
        DEBUG_LOG("kymeraAdaptiveAnc_DeactivateRxSplitterSecondOutput");
        OperatorsSplitterEnableSecondOutput(splitter_op, FALSE);
    }
}

static void kymeraAdaptiveAnc_ActivateRxSplitterSecondOutput(void)
{
    if(kymeraAdaptiveAnc_IsRxSplitterCreated())
    {    
        Operator splitter_op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_RX_SPLITTER), OPR_AANC_SPLT_SCO_RX);
        DEBUG_LOG("kymeraAdaptiveAnc_ActivateRxSplitterSecondOutput");
        OperatorsSplitterEnableSecondOutput(splitter_op, TRUE);
    }
}

static void kymeraAdaptiveAnc_ConnectRxSplitterToAanc(void)
{
    if(kymeraAdaptiveAnc_IsAancActive() && kymeraAdaptiveAnc_IsRxSplitterCreated())
    {
        Source spl_sco_src;
        Sink aanc_sco_in;
        
        DEBUG_LOG("kymeraAdaptiveAnc_ConnectRxSplitterToAanc");
        
        /* Connect splitter output 2 to core Adaptive ANC sco input */
        spl_sco_src = kymeraAdaptiveAnc_GetOutput(CHAIN_RX_SPLITTER, EPR_SPLT_OUT_ANC_VAD);
        aanc_sco_in = kymeraAdaptiveAnc_GetInput(CHAIN_AANC, EPR_AANC_VOICE_DECTECTION_IN);
        PanicNull(StreamConnect(spl_sco_src,aanc_sco_in));
    }
}

static void kymeraAdaptiveAnc_ConfigureBasicPass(void)
{
    Operator op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_TX_SPLITTER), OPR_AANC_BASIC_PASS);
#define INITIAL_OPERATOR_GAIN    (0U)

    OperatorsSetPassthroughDataFormat(op, operator_data_format_pcm);
    OperatorsSetPassthroughGain(op, INITIAL_OPERATOR_GAIN);
}

static void kymeraAdaptiveAnc_ConfigureTxSplitterChain(void)
{
    Operator op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_TX_SPLITTER), OPR_AANC_SPLT_SCO_TX);
    OperatorsSplitterSetWorkingMode(op, splitter_mode_clone_input);
    OperatorsSplitterEnableSecondOutput(op, FALSE);
    OperatorsSplitterSetDataFormat(op, operator_data_format_pcm);
}

static void kymeraAdaptiveAnc_CreateTxSplitterChain(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_CreateTxSplitterChain");
    PanicNotNull(kymeraAdaptiveAnc_GetChain(CHAIN_TX_SPLITTER));
    kymeraAdaptiveAnc_SetChain(CHAIN_TX_SPLITTER, PanicNull(ChainCreate(Kymera_GetChainConfigs()->chain_aanc_splitter_sco_tx_path_config)));
    kymeraAdaptiveAnc_ConfigureTxSplitterChain();
    kymeraAdaptiveAnc_ConfigureBasicPass();
    ChainConnect(kymeraAdaptiveAnc_GetChain(CHAIN_TX_SPLITTER));
}

static void kymeraAdaptiveAnc_ActivateTxSplitterSecondOutput(void)
{
    if(kymeraAdaptiveAnc_IsTxSplitterCreated())
    {    
        Operator splitter_op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_TX_SPLITTER), OPR_AANC_SPLT_SCO_TX);
        DEBUG_LOG("kymeraAdaptiveAnc_ActivateTxSplitterSecondOutput");
        OperatorsSplitterEnableSecondOutput(splitter_op, TRUE);
    }
}

static void kymeraAdaptiveAnc_DeactivateTxSplitterSecondOutput(void)
{
    if(kymeraAdaptiveAnc_IsTxSplitterCreated())
    {    
        Operator splitter_op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_TX_SPLITTER), OPR_AANC_SPLT_SCO_TX);
        DEBUG_LOG("kymeraAdaptiveAnc_DeactivateTxSplitterSecondOutput");
        OperatorsSplitterEnableSecondOutput(splitter_op, FALSE);
    }
}

static void kymeraAdaptiveAnc_DestroyTxSplitterChain(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_DestroyTxSplitterChain");
    PanicNull(kymeraAdaptiveAnc_GetChain(CHAIN_TX_SPLITTER));
    ChainStop(kymeraAdaptiveAnc_GetChain(CHAIN_TX_SPLITTER));
    ChainDestroy(kymeraAdaptiveAnc_GetChain(CHAIN_TX_SPLITTER));
    kymeraAdaptiveAnc_SetChain(CHAIN_TX_SPLITTER, NULL);
}

static void kymeraAdaptiveAnc_ConfigureTxResamplerChain(void)
{
    Operator op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_TX_RESAMPLER), OPR_AANC_DOWN_SAMPLE);
    OperatorsResamplerSetConversionRate(op, AANC_SAMPLE_RATE, 8000);
}

static void kymeraAdaptiveAnc_CreateTxResamplerChain(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_CreateTxResamplerChain");
    PanicNotNull(kymeraAdaptiveAnc_GetChain(CHAIN_TX_RESAMPLER));
    kymeraAdaptiveAnc_SetChain(CHAIN_TX_RESAMPLER, PanicNull(ChainCreate(Kymera_GetChainConfigs()->chain_aanc_resampler_sco_tx_path_config)));
    kymeraAdaptiveAnc_ConfigureTxResamplerChain();
    ChainConnect(kymeraAdaptiveAnc_GetChain(CHAIN_TX_RESAMPLER));
}

static void kymeraAdaptiveAnc_DestroyTxResamplerChain(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_DestroyTxResamplerChain");
    PanicNull(kymeraAdaptiveAnc_GetChain(CHAIN_TX_RESAMPLER));
    ChainStop(kymeraAdaptiveAnc_GetChain(CHAIN_TX_RESAMPLER));
    ChainDestroy(kymeraAdaptiveAnc_GetChain(CHAIN_TX_RESAMPLER));
    kymeraAdaptiveAnc_SetChain(CHAIN_TX_RESAMPLER, NULL);
}

static void kymeraAdaptiveAnc_ConnectTxSplitterToAanc(void)
{
    if(kymeraAdaptiveAnc_IsAancActive() && kymeraAdaptiveAnc_IsTxSplitterCreated())
    {
        Source spl_mic_src;
        Sink aanc_ff_in;
        
        DEBUG_LOG("kymeraAdaptiveAnc_ConnectTxSplitterToAanc");
        
        /* Connect splitter output 2 to core Adaptive ANC FF input */
        spl_mic_src = kymeraAdaptiveAnc_GetOutput(CHAIN_TX_SPLITTER, EPR_SPLT_OUT_ANC_FF);
        aanc_ff_in = kymeraAdaptiveAnc_GetInput(CHAIN_AANC, EPR_AANC_FF_MIC_IN);
        PanicNull(StreamConnect(spl_mic_src, aanc_ff_in));
    }
}

static Sink kymeraAdaptiveAnc_GetInputForRole(chain_endpoint_role_t role)
{
    Sink sink = NULL;
    switch(role)
    {
        case EPR_SPLT_SCO_IN:
            sink = kymeraAdaptiveAnc_GetInput(CHAIN_RX_SPLITTER, role);
            break;            
           
        case EPR_IIR_16K_IN1: /* fall-thru */
        case EPR_IIR_16K_IN2: 
        case EPR_IIR_AEC_REF_16K_IN:
            sink = kymeraAdaptiveAnc_GetInput(CHAIN_TX_RESAMPLER, role);
            break;

        case EPR_AANC_FF_MIC_IN: /* fall-thru */
        case EPR_AANC_ERR_MIC_IN:
            sink = kymeraAdaptiveAnc_GetInput(CHAIN_AANC, role);
            break;

        default:
            sink = NULL;
            break;
    }
    return sink;
}

/*! Utility function to check if cVc mic is shared with ANC FF */
static bool kymeraAdaptiveAnc_IsMicSharedWithAnc(void)
{
    /* It can so happen that SCO can support 2 mic, however due to system limitation VA can support only 1mic */
    if (Kymera_IsVaCaptureActive())
        return  ((appConfigVaMic2() == appConfigAncFeedForwardMic()) || (appConfigVaMic1() == appConfigAncFeedForwardMic())) ? TRUE : FALSE;
    else if (appKymeraIsScoActive())
        return  ((appConfigMicExternal() == appConfigAncFeedForwardMic()) || (appConfigMicVoice() == appConfigAncFeedForwardMic())) ? TRUE : FALSE;
    return FALSE;
}

/*! Utility function to check if cVc mic 1 is shared with ANC FF */
static bool kymeraAdaptiveAnc_IsScoMic1SharedWithAnc(void)
{
    if (appKymeraIsScoActive())
        return  (appConfigMicVoice() == appConfigAncFeedForwardMic()) ? TRUE : FALSE;
    else
        return FALSE;
}

/*! Utility function to check if cVc mic 2 is shared with ANC FF */
static bool kymeraAdaptiveAnc_IsScoMic2SharedWithAnc(void)
{
    if (appKymeraIsScoActive())
        return  (appConfigMicExternal() == appConfigAncFeedForwardMic()) ? TRUE : FALSE;
    else
        return FALSE;
}


/*!Create AANC chain in Standby mode if not already enabled during SCO call
*/
static void kymeraAdaptiveAnc_CreateChainForConcurrency(void)
{
    /*Concurrency scenario when AANC is to be enabled first time and capability needs to be in Standby mode*/
    if  (!kymeraAdaptiveAnc_IsAancActive())
    {
        KYMERA_INTERNAL_AANC_ENABLE_T param;
        AncStateManager_GetAdaptiveAncEnableParams(&param.in_ear, &param.control_path, &param.hw_channel, &param.current_mode);
        kymeraAdaptiveAnc_CreateAancChain(&param, aanc_usecase_sco_concurrency);
    }
}

/*!Start SCO Send and Receive Resampler and Splitter operators
*/
static void kymeraAdaptiveAnc_StartScoSendReceive(void)
{
    if(kymeraAdaptiveAnc_IsTxResamplerCreated())
        ChainStart(kymeraAdaptiveAnc_GetChain(CHAIN_TX_RESAMPLER));

    if(kymeraAdaptiveAnc_IsRxResamplerCreated())
        ChainStart(kymeraAdaptiveAnc_GetChain(CHAIN_RX_RESAMPLER));

    if(kymeraAdaptiveAnc_IsTxSplitterCreated())
        ChainStart(kymeraAdaptiveAnc_GetChain(CHAIN_TX_SPLITTER));

    if(kymeraAdaptiveAnc_IsRxSplitterCreated())
        ChainStart(kymeraAdaptiveAnc_GetChain(CHAIN_RX_SPLITTER));
}

/*!Configure non shared mics (both FF and FB)
*/
static void kymeraAdaptiveAnc_ConfigureNonSharedMic(aec_connect_audio_input_t *aec_input_param, Source source_to_synchronise_with)
{
    uint8 nr_sco_mics = Kymera_GetNumberOfMics();

    /* Adaptive ANC should share the mic with SCO or VA, so it should be non-exclusive user */
    Source mic_src_ff = Kymera_GetMicrophoneSource(appConfigAncFeedForwardMic(), NULL, appKymeraGetOptimalMicSampleRate(ADAPTIVE_ANC_MIC_SAMPLE_RATE), non_exclusive_user);
    Source mic_src_fb = Kymera_GetMicrophoneSource(appConfigAncFeedBackMic(), mic_src_ff, appKymeraGetOptimalMicSampleRate(ADAPTIVE_ANC_MIC_SAMPLE_RATE), non_exclusive_user);

    UNUSED(source_to_synchronise_with);
    DEBUG_LOG("kymeraAdaptiveAnc_ConfigureNonSharedMic");

    /* Fill the AEC Ref Input params */    
    switch (nr_sco_mics)
    {
        case 1:
            {
                aec_input_param->mic_input_2 = mic_src_ff;
                aec_input_param->mic_input_3 = mic_src_fb;

                aec_input_param->mic_output_2 = kymeraAdaptiveAnc_GetInput(CHAIN_AANC, EPR_AANC_FF_MIC_IN);
                aec_input_param->mic_output_3 = kymeraAdaptiveAnc_GetInput(CHAIN_AANC, EPR_AANC_ERR_MIC_IN);
            }
            break;
            
        case 2:
            {
                aec_input_param->mic_input_3 = mic_src_ff;
                aec_input_param->mic_input_4 = mic_src_fb;
                
                aec_input_param->mic_output_3 = kymeraAdaptiveAnc_GetInput(CHAIN_AANC, EPR_AANC_FF_MIC_IN);
                aec_input_param->mic_output_4 = kymeraAdaptiveAnc_GetInput(CHAIN_AANC, EPR_AANC_ERR_MIC_IN);
            }
            break;

        case 3:
            /*If SCO mics are three and non-shared with AANC, this is unsupported configuration*/
            DEBUG_LOG("kymeraAdaptiveAnc_ConfigureNonSharedMic: Unsupported CVC Mic Configuration");
            break;
            
            default:
                break;            
    }
}

/*!Configure shared mics (FF) with CVC. Configure FB mic.
*/
static void kymeraAdaptiveAnc_ConfigureSharedMic(aec_connect_audio_input_t *aec_input_param, Source source_to_sync_with)
{  
    Source mic_src_1b = Kymera_GetMicrophoneSource(appConfigAncFeedBackMic(), source_to_sync_with, appKymeraGetOptimalMicSampleRate(ADAPTIVE_ANC_MIC_SAMPLE_RATE), non_exclusive_user);

    DEBUG_LOG("kymeraAdaptiveAnc_ConfigureSharedMic");

    /*Create Tx Splitter for Shared mics*/
    /*Also creates Basic Passthrough to connect to Tx splitter, to avert issues with synchronisation and data loss at splitter*/
    if(!kymeraAdaptiveAnc_IsTxSplitterCreated())
        kymeraAdaptiveAnc_CreateTxSplitterChain();
    
    if (kymeraAdaptiveAnc_IsScoMic1SharedWithAnc())
    {
        /*Connect AEC mic output to Basic Pass input*//*mic_input_1 will be CVC primary shared with FF*/
        aec_input_param->mic_output_1 = kymeraAdaptiveAnc_GetInput(CHAIN_TX_SPLITTER, EPR_AANC_BASIC_PASS_IN);
    }
    else if (kymeraAdaptiveAnc_IsScoMic2SharedWithAnc())
    {
        /*Connect AEC mic output to Basic Pass input*//*mic_input_1 will be CVC secondary shared with FF*/
        aec_input_param->mic_output_2 = kymeraAdaptiveAnc_GetInput(CHAIN_TX_SPLITTER, EPR_AANC_BASIC_PASS_IN);
    }

    /*FB mic configuration, FB is not shared*/
    if (aec_input_param->mic_input_2 == NULL)
    {
        aec_input_param->mic_input_2 = mic_src_1b;
        aec_input_param->mic_output_2 = kymeraAdaptiveAnc_GetInput(CHAIN_AANC, EPR_AANC_ERR_MIC_IN);
    }
    else if (aec_input_param->mic_input_3 == NULL)
    {
        aec_input_param->mic_input_3 = mic_src_1b;
        aec_input_param->mic_output_3 = kymeraAdaptiveAnc_GetInput(CHAIN_AANC, EPR_AANC_ERR_MIC_IN);
    }
    else
    {
        aec_input_param->mic_input_4 = mic_src_1b;
        aec_input_param->mic_output_4 = kymeraAdaptiveAnc_GetInput(CHAIN_AANC, EPR_AANC_ERR_MIC_IN);
    }
}


/*!Configure mics (FF and FB) for SCO concurrency use case
*/
static void kymeraAdaptiveAnc_ConfigureMicsForConcurrency(aec_connect_audio_input_t *aec_input_param, Source source_to_sync_with)
{   
    if (kymeraAdaptiveAnc_IsMicSharedWithAnc())
    {
        kymeraAdaptiveAnc_ConfigureSharedMic(aec_input_param, source_to_sync_with);
    }
    else
    {
        kymeraAdaptiveAnc_ConfigureNonSharedMic(aec_input_param, source_to_sync_with);
    }
}

/*!Connect SCO Send operators
*/
static void kymeraAdaptiveAnc_ConnectScoSend(const adaptive_anc_sco_send_t *sco_send)
{
    kymeraAdaptiveAnc_CreateTxResamplerChain();
    
    /* Connect down-sampler output to SCO Tx inputs */
    PanicNull(StreamConnect(kymeraAdaptiveAnc_GetOutput(CHAIN_TX_RESAMPLER, EPR_IIR_AEC_REF_8K_OUT), sco_send->cVc_ref_in));
    PanicNull(StreamConnect(kymeraAdaptiveAnc_GetOutput(CHAIN_TX_RESAMPLER, EPR_IIR_8K_OUT1), sco_send->cVc_in1));
    if(sco_send->cVc_in2)
        PanicNull(StreamConnect(kymeraAdaptiveAnc_GetOutput(CHAIN_TX_RESAMPLER, EPR_IIR_8K_OUT2), sco_send->cVc_in2));
}

/*!Connect Tx Splitter to Tx Resampler in case of SCO NB use case
*/
static void kymeraAdaptiveAnc_ConnectSplitterOutputToScoTxResampler(void)
{
    if ((kymeraAdaptiveAnc_IsTxSplitterCreated()) && (kymeraAdaptiveAnc_GetInputForRole(EPR_IIR_16K_IN1) != NULL))
    {
        DEBUG_LOG("kymeraAdaptiveAnc_ConnectSplitterOutputToScoTxResampler");

        if ((kymeraAdaptiveAnc_IsMicSharedWithAnc()) && (kymeraAdaptiveAnc_GetOutputForRole(EPR_SPLT_OUT_CVC_SEND) != NULL))
        {
            chain_endpoint_role_t role=EPR_IIR_16K_IN1;

            if (kymeraAdaptiveAnc_IsScoMic1SharedWithAnc())
            {
                role=EPR_IIR_16K_IN1;
            }
            else if (kymeraAdaptiveAnc_IsScoMic2SharedWithAnc())
            {
                role=EPR_IIR_16K_IN2;
            }
            
            PanicNull(StreamConnect(kymeraAdaptiveAnc_GetOutputForRole(EPR_SPLT_OUT_CVC_SEND), kymeraAdaptiveAnc_GetInputForRole(role)));
        }
    }
}

/*!Connect Tx Splitter to CVC Send operator in case of SCO WB use case
*/
static void kymeraScoConnectSplitterOutputToCvcSend(void)
{
    if (kymeraAdaptiveAnc_IsTxSplitterCreated())
    {
        kymera_chain_handle_t sco_chain = PanicNull(appKymeraGetScoChain());

        DEBUG_LOG("kymeraScoConnectSplitterOutputToCvcSend");

        if ((kymeraAdaptiveAnc_IsMicSharedWithAnc()) && (kymeraAdaptiveAnc_GetOutputForRole(EPR_SPLT_OUT_CVC_SEND) != NULL))
        {
            chain_endpoint_role_t role=EPR_CVC_SEND_IN1;

            if (kymeraAdaptiveAnc_IsScoMic1SharedWithAnc())
            {
                role=EPR_CVC_SEND_IN1;
            }
            else if (kymeraAdaptiveAnc_IsScoMic2SharedWithAnc())
            {
                role=EPR_CVC_SEND_IN2;
            }
            
            PanicNull(StreamConnect(kymeraAdaptiveAnc_GetOutputForRole(EPR_SPLT_OUT_CVC_SEND), ChainGetInput(sco_chain, role)));
        }
    }
}

/*Selected mic to be disconnected when concurrent with SCO*/
static void kymerAdaptiveAnc_DisconnectMic_ForConcurrency(void)
{
    aec_disconnect_audio_input_t aec_disconnect_input_param = {0};
    uint8 nr_sco_mics = Kymera_GetNumberOfMics();

    /*Assumption that FB mic is never shared, only FF is shared*/
    if (kymeraAdaptiveAnc_IsMicSharedWithAnc())
    {
        if (nr_sco_mics==1)
        {
            DEBUG_LOG("kymerAdaptiveAnc_DisconnectMic_ForConcurrency shared 1 mic cvc");
            aec_disconnect_input_param.mic_disconnect_bitmap = AEC_BITMAP_MIC2; /*Disconnect FB, FF is shared with SCO, do not disconnect primary*/
        }
        else if (nr_sco_mics==2)
        {
            DEBUG_LOG("kymerAdaptiveAnc_DisconnectMic_ForConcurrency shared 2 mic cvc");
            aec_disconnect_input_param.mic_disconnect_bitmap = AEC_BITMAP_MIC3; /*Disconnect FB, FF is shared with SCO, do not disconnect primary*/
        }
        else if (nr_sco_mics==3)
        {
            DEBUG_LOG("kymerAdaptiveAnc_DisconnectMic_ForConcurrency shared 3mic cvc");
            aec_disconnect_input_param.mic_disconnect_bitmap = AEC_BITMAP_MIC4; /*Disconnect FB, FF is shared with SCO, do not disconnect primary*/
        }

        Kymera_DisconnectSelectedAudioInputFromAec(&aec_disconnect_input_param);
        Kymera_CloseMicrophone(appConfigAncFeedBackMic(), non_exclusive_user);
    }
    else /*non-shared */
    {
        if (nr_sco_mics==1)
        {
            DEBUG_LOG("kymerAdaptiveAnc_DisconnectMic_ForConcurrency shared 1 mic cvc");
            aec_disconnect_input_param.mic_disconnect_bitmap = AEC_BITMAP_MIC2 | AEC_BITMAP_MIC3; /*Disconnect FF and FB*/
        }
        else if (nr_sco_mics==2)
        {
            DEBUG_LOG("kymerAdaptiveAnc_DisconnectMic_ForConcurrency shared 2 mic cvc");
            aec_disconnect_input_param.mic_disconnect_bitmap = AEC_BITMAP_MIC3| AEC_BITMAP_MIC4; /*Disconnect FF and FB*/
        }
        else if (nr_sco_mics==3)
        {
            DEBUG_LOG("kymerAdaptiveAnc_DisconnectMic_ForConcurrency shared 3mic cvc with AANC no-shared mics unsupported");
        }

        Kymera_DisconnectSelectedAudioInputFromAec(&aec_disconnect_input_param);
        Kymera_CloseMicrophone(appConfigAncFeedForwardMic(), non_exclusive_user);
        Kymera_CloseMicrophone(appConfigAncFeedBackMic(), non_exclusive_user);
    }
}

/*! \brief Connect Re-sampler output from Adaptive ANC down-sampler to SCO Tx
    \param sco_send - Sink(s) of SCO cVc send
*/
static void kymeraAdaptiveAnc_DisconnectScoSend(void)
{
    if(kymeraAdaptiveAnc_GetChain(CHAIN_TX_RESAMPLER))
    {
        StreamDisconnect(kymeraAdaptiveAnc_GetOutput(CHAIN_TX_RESAMPLER, EPR_IIR_AEC_REF_8K_OUT), NULL);
        StreamDisconnect(kymeraAdaptiveAnc_GetOutput(CHAIN_TX_RESAMPLER, EPR_IIR_8K_OUT1), NULL);
        StreamDisconnect(kymeraAdaptiveAnc_GetOutput(CHAIN_TX_RESAMPLER, EPR_IIR_8K_OUT2), NULL);
        kymeraAdaptiveAnc_DestroyTxResamplerChain();
    }
}

static void KymeraAdaptiveAnc_ConfigureAec(void)
{
    aec_connect_audio_input_t aec_input_param = {0};
    aec_audio_config_t aec_config = {0};
    DEBUG_LOG("KymeraAdaptiveAnc_ConfigureAec");

    /* Adaptive ANC should share the mic with SCO or VA, so it should be non-exclusive user */
    Source mic_src_1a = Kymera_GetMicrophoneSource(appConfigAncFeedForwardMic(), NULL, appKymeraGetOptimalMicSampleRate(ADAPTIVE_ANC_MIC_SAMPLE_RATE), non_exclusive_user);
    Source mic_src_1b = Kymera_GetMicrophoneSource(appConfigAncFeedBackMic(), mic_src_1a, appKymeraGetOptimalMicSampleRate(ADAPTIVE_ANC_MIC_SAMPLE_RATE), non_exclusive_user);

    /* Fill the AEC Ref Input params */
    aec_input_param.mic_input_1 = mic_src_1a;
    aec_input_param.mic_input_2 = mic_src_1b;
    
    aec_input_param.mic_output_1 = kymeraAdaptiveAnc_GetInput(CHAIN_AANC, EPR_AANC_FF_MIC_IN);
    aec_input_param.mic_output_2 = kymeraAdaptiveAnc_GetInput(CHAIN_AANC, EPR_AANC_ERR_MIC_IN);

    /* sample rate */
    aec_config.spk_sample_rate = AANC_SAMPLE_RATE;
    aec_config.mic_sample_rate = appKymeraGetOptimalMicSampleRate(ADAPTIVE_ANC_MIC_SAMPLE_RATE);

    Kymera_SetAecUseCase(aec_usecase_adaptive_anc);
    Kymera_ConnectAudioInputToAec(&aec_input_param, &aec_config);
}

static void kymeraAdaptiveAnc_DestroySplitter(void)
{
    if(kymeraAdaptiveAnc_IsRxSplitterCreated())
    {
        kymeraAdaptiveAnc_DestroyRxSplitterChain();    
    }

    if(kymeraAdaptiveAnc_IsTxSplitterCreated())
    {
        kymeraAdaptiveAnc_DestroyTxSplitterChain();
    }
}

static void kymeraAdaptiveAnc_DisableAancInScoActive(void)
{
    Operator op_aanc;
    kymeraAdaptiveAnc_PanicIfNotActive();
    
    DEBUG_LOG("kymeraAdaptiveAnc_DisableAancInScoActive");  
    op_aanc = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_AANC), OPR_AANC);
    OperatorsAdaptiveAncSetModeOverrideCtrl(op_aanc, adaptive_anc_mode_standby);
    DEBUG_LOG("AANC moved to Standby due to Disable during SCO call");
}

static void kymeraAdaptiveAnc_EnableAancInScoActive(const KYMERA_INTERNAL_AANC_ENABLE_T* msg)
{
    DEBUG_LOG("kymeraAdaptiveAnc_EnableAancInScoActive");

    Operator op_aanc;
    kymeraAdaptiveAnc_PanicIfNotActive();

    op_aanc = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_AANC), OPR_AANC);

    if (msg->current_mode == anc_mode_1)
    {
        OperatorsAdaptiveAncSetModeOverrideCtrl(op_aanc, adaptive_anc_mode_full);
        OperatorsAdaptiveAncSetInEarCtrl(op_aanc, msg->in_ear);
        DEBUG_LOG("AANC comes up in Full Proc");
    }
    else
    {       
        OperatorsAdaptiveAncSetModeOverrideCtrl(op_aanc, adaptive_anc_mode_static);
        OperatorsAdaptiveAncSetInEarCtrl(op_aanc, msg->in_ear);
        DEBUG_LOG("AANC comes up in Static");
    }
}

static void kymerAdaptiveAnc_CreateStandalone(const KYMERA_INTERNAL_AANC_ENABLE_T* param)
{
    PanicNull((void *)(param));    
    DEBUG_LOG("kymerAdaptiveAnc_CreateStandalone");

    kymeraAdaptiveAnc_CreateAancChain(param, aanc_usecase_standalone);

    /* DSP needs to run minimum at 32MHz (AUDIO_DSP_SLOW_CLOCK). 
    In the below interface, if state is either KYMERA_STATE_IDLE or KYMERA_STATE_ADAPTIVE_ANC (new state) 
    the default DSP active mode value is AUDIO_DSP_SLOW_CLOCK. In anyother case is more than this */
    appKymeraConfigureDspPowerMode();

    KymeraAdaptiveAnc_ConfigureAec();
    ChainStart(kymeraAdaptiveAnc_GetChain(CHAIN_AANC));
}

static void kymerAdaptiveAnc_DestroyAndDisconnectStandalone(void)
{
    DEBUG_LOG("kymerAdaptiveAnc_DestroyAndDisconnectStandalone");

    ChainStop(kymeraAdaptiveAnc_GetChain(CHAIN_AANC));
    Kymera_DisconnectAudioInputFromAec();
    Kymera_CloseMicrophone(appConfigAncFeedForwardMic(), non_exclusive_user);
    Kymera_CloseMicrophone(appConfigAncFeedBackMic(), non_exclusive_user);

    kymeraAdaptiveAnc_DestroyAancChain();
    Kymera_SetAecUseCase(aec_usecase_default);
}

#else
static void kymeraAdaptiveAnc_CreateFbcChain(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_CreateFbcChain");
    if (!kymeraAdaptiveAnc_IsAancFbcActive())
    {
        PanicNotNull(kymeraAdaptiveAnc_GetChain(CHAIN_FBC));
        kymeraAdaptiveAnc_SetChain(CHAIN_FBC, PanicNull(ChainCreate(Kymera_GetChainConfigs()->chain_aanc_fbc_config)));        
        PanicFalse(Kymera_SetOperatorUcid(kymeraAdaptiveAnc_GetChain(CHAIN_FBC), OPR_AANC_FBC_ERR_MIC_PATH, UCID_ADAPTIVE_ANC_FBC));
        PanicFalse(Kymera_SetOperatorUcid(kymeraAdaptiveAnc_GetChain(CHAIN_FBC), OPR_AANC_FBC_FF_MIC_PATH, UCID_ADAPTIVE_ANC_FBC));
        ChainConnect(kymeraAdaptiveAnc_GetChain(CHAIN_FBC));
    }
}

static void kymeraAdaptiveAnc_DestroyFbcChain(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_DestroyFbcChain");
    if (kymeraAdaptiveAnc_IsAancFbcActive())
    {
        PanicNull(kymeraAdaptiveAnc_GetChain(CHAIN_FBC));
        ChainDestroy(kymeraAdaptiveAnc_GetChain(CHAIN_FBC));
        kymeraAdaptiveAnc_SetChain(CHAIN_FBC, NULL);
    }
}

static void kymeraAdaptiveAnc_CreateSplitterChainInMicRefPath(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_CreateSplitterChainInMicRefPath");
    if (!kymeraAdaptiveAnc_IsSplitterInMicRefPathCreated())
    {
        Operator op;       
        PanicNotNull(kymeraAdaptiveAnc_GetChain(CHAIN_MIC_REF_PATH_SPLITTER));
        kymeraAdaptiveAnc_SetChain(CHAIN_MIC_REF_PATH_SPLITTER, PanicNull(ChainCreate(Kymera_GetChainConfigs()->chain_aanc_splitter_mic_ref_path_config)));

        op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_MIC_REF_PATH_SPLITTER), OPR_AANC_SPLT_MIC_REF_PATH);
        if(op)
        {
            OperatorsSplitterSetWorkingMode(op, splitter_mode_clone_input);   
            OperatorsSplitterEnableSecondOutput(op, FALSE);
            OperatorsSplitterSetDataFormat(op, operator_data_format_pcm);
        }
        ChainConnect(kymeraAdaptiveAnc_GetChain(CHAIN_MIC_REF_PATH_SPLITTER));
    }
}

static void kymeraAdaptiveAnc_DestroySplitterChainInMicRefPath(void)
{
    if (kymeraAdaptiveAnc_IsSplitterInMicRefPathCreated())
    {
        DEBUG_LOG("kymeraAdaptiveAnc_DestroySplitterChainInMicRefPath");
        PanicNull(kymeraAdaptiveAnc_GetChain(CHAIN_MIC_REF_PATH_SPLITTER));
        ChainDestroy(kymeraAdaptiveAnc_GetChain(CHAIN_MIC_REF_PATH_SPLITTER));
        kymeraAdaptiveAnc_SetChain(CHAIN_MIC_REF_PATH_SPLITTER, NULL);
    }
}

static void kymeraAdaptiveAnc_ActivateMicRefPathSplitterSecondOutput(void)
{
    if(kymeraAdaptiveAnc_IsSplitterInMicRefPathCreated())
    {
        Operator splt_op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_MIC_REF_PATH_SPLITTER), OPR_AANC_SPLT_MIC_REF_PATH);
        DEBUG_LOG("kymeraAdaptiveAnc_ActivateMicRefPathSplitterSecondOutput");
        OperatorsSplitterEnableSecondOutput(splt_op, TRUE);
    }
}

static void kymeraAdaptiveAnc_DeactivateMicRefPathSplitterSecondOutput(void)
{
    if(kymeraAdaptiveAnc_IsSplitterInMicRefPathCreated())
    {
        Operator splt_op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_MIC_REF_PATH_SPLITTER), OPR_AANC_SPLT_MIC_REF_PATH);
        DEBUG_LOG("kymeraAdaptiveAnc_DeactivateMicRefPathSplitterSecondOutput");
        OperatorsSplitterEnableSecondOutput(splt_op, FALSE);
    }
}

static void kymeraAdaptiveAnc_ConnectFbcChain(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_ConnectFbcChain");
    if(kymeraAdaptiveAnc_IsAancActive() && kymeraAdaptiveAnc_IsAancFbcActive())
    {
        /* Connect FBC and AANC operators */
        PanicNull(StreamConnect(kymeraAdaptiveAnc_GetOutput(CHAIN_FBC, EPR_AANC_FBC_FF_MIC_OUT), kymeraAdaptiveAnc_GetInput(CHAIN_AANC, EPR_AANC_FF_MIC_IN)));
        PanicNull(StreamConnect(kymeraAdaptiveAnc_GetOutput(CHAIN_FBC, EPR_AANC_FBC_ERR_MIC_OUT), kymeraAdaptiveAnc_GetInput(CHAIN_AANC, EPR_AANC_ERR_MIC_IN)));
    }
}

static void kymeraAdaptiveAnc_ConnectSplitterFbcChain(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_ConnectSplitterFbcChain");
    if((kymeraAdaptiveAnc_IsSplitterInMicRefPathCreated()) && kymeraAdaptiveAnc_IsAancFbcActive())
    {
        /* Connect Splitter and FBC operators */
        PanicNull(StreamConnect(kymeraAdaptiveAnc_GetOutput(CHAIN_MIC_REF_PATH_SPLITTER, EPR_SPLT_MIC_REF_OUT1), kymeraAdaptiveAnc_GetInput(CHAIN_FBC, EPR_AANC_FBC_FF_MIC_REF_IN)));
        PanicNull(StreamConnect(kymeraAdaptiveAnc_GetOutput(CHAIN_MIC_REF_PATH_SPLITTER, EPR_SPLT_MIC_REF_OUT2), kymeraAdaptiveAnc_GetInput(CHAIN_FBC, EPR_AANC_FBC_ERR_MIC_REF_IN)));
    }
}

static void kymerAdaptiveAnc_DisconnectFbcOutputs(void)
{
    if(kymeraAdaptiveAnc_IsAancFbcActive())
    {
        StreamDisconnect(kymeraAdaptiveAnc_GetOutput(CHAIN_FBC, EPR_AANC_FBC_FF_MIC_OUT), NULL);
        StreamDisconnect(kymeraAdaptiveAnc_GetOutput(CHAIN_FBC, EPR_AANC_FBC_ERR_MIC_OUT), NULL);
    }
}

static void kymerAdaptiveAnc_DisconnectMicRefPathSplitterOutputs(void)
{
    if(kymeraAdaptiveAnc_IsSplitterInMicRefPathCreated())
    {
        StreamDisconnect(kymeraAdaptiveAnc_GetOutput(CHAIN_MIC_REF_PATH_SPLITTER, EPR_SPLT_MIC_REF_OUT1), NULL);
        StreamDisconnect(kymeraAdaptiveAnc_GetOutput(CHAIN_MIC_REF_PATH_SPLITTER, EPR_SPLT_MIC_REF_OUT2), NULL);
    }
}

static bool kymeraAdapativeAnc_IsMusicActive(void)
{
    if((appKymeraGetState() >= KYMERA_STATE_A2DP_STARTING_A) && (appKymeraGetState() <=KYMERA_STATE_A2DP_STREAMING_WITH_FORWARDING))
        return TRUE;

    return FALSE;
}

/*Add only concurrent user requiring FBC here*/
static bool kymeraAdaptiveAnc_IsConcurrentUser(mic_users_t active_user)
{
    if(((active_user & mic_user_sco) == mic_user_sco) ||
       ((active_user & mic_user_scofwd) == mic_user_scofwd) ||
       ((active_user & mic_user_va) == mic_user_va) ||
       ((active_user & mic_user_usb_voice) == mic_user_usb_voice))
    {
        return TRUE;
    }
    return FALSE;
}

/* <Note> This static interface should not be called in Mic framework callback functions */
static bool kymeraAdaptiveAnc_IsVaSessionActive(void)
{
    mic_users_t active_user = Kymera_MicGetActiveUsers();

    if((active_user & mic_user_va) == mic_user_va)
    {
        return TRUE;
    }
    return FALSE;
}

static bool kymeraAdapativeAnc_IsVoiceActive(void)
{
    return kymeraAdaptiveAnc_IsConcurrentUser(Kymera_MicGetActiveUsers());
}

static bool kymeraAdapativeAnc_IsConcurrencyAudioSourceActive(void)
{
    return (kymeraAdapativeAnc_IsMusicActive() || kymeraAdapativeAnc_IsVoiceActive());
}

static void kymeraAdaptiveAnc_CreateEchoCancellerChain(void)
{
    DEBUG_LOG("kymeraAdaptiveAnc_CreateEchoCancellerChain");
    kymeraAdaptiveAnc_CreateFbcChain();
    kymeraAdaptiveAnc_CreateSplitterChainInMicRefPath();
}

static void kymeraAdaptiveAnc_DestroyEchoCancellerChain(void)
{
    kymeraAdaptiveAnc_DestroySplitterChainInMicRefPath();
    kymeraAdaptiveAnc_DestroyFbcChain();
}

static void kymeraAdaptiveAnc_ConnectConcurrency(void)
{
    kymeraAdaptiveAnc_ConnectFbcChain();
    kymeraAdaptiveAnc_ConnectSplitterFbcChain();
    kymeraAdaptiveAnc_ActivateMicRefPathSplitterSecondOutput();
}

/*! \brief Enable AANC concurrency graph.
    \param transition - TRUE for concurrency graph enable during transition from standalone to concurrency
                      - FALSE for concurrency graph enable in full enable case
    \return void
*/
static void kymeraAdaptiveAnc_StartConcurrency(bool transition)
{
    if(transition)
    {
        /* Enable Adaptivity during transitional case */
        kymeraAdaptiveAnc_UpdateAdaptivity(ENABLE_ADAPTIVITY);
    }
    else
    {
        /* start AANC chain in case of full enable */
        ChainStart(kymeraAdaptiveAnc_GetChain(CHAIN_AANC));
    }
    ChainStart(kymeraAdaptiveAnc_GetChain(CHAIN_FBC));
    ChainStart(kymeraAdaptiveAnc_GetChain(CHAIN_MIC_REF_PATH_SPLITTER));
}

static void kymeraAdaptiveAnc_StopConcurrency(void)
{
    if (kymeraAdaptiveAnc_IsAancActive())
    {
        if(kymeraAdaptiveAnc_IsSplitterInMicRefPathCreated())
        {
            ChainStop(kymeraAdaptiveAnc_GetChain(CHAIN_MIC_REF_PATH_SPLITTER));
        }
        if(kymeraAdaptiveAnc_IsAancFbcActive())
        {
            ChainStop(kymeraAdaptiveAnc_GetChain(CHAIN_FBC));
        }
        /* Disable adaptivity during transition from concurrency to standalone. */
        kymeraAdaptiveAnc_UpdateAdaptivity(DISABLE_ADAPTIVITY);
    }
}

static void kymeraAdaptiveAnc_DisconnectConcurrency(void)
{
    kymeraAdaptiveAnc_DeactivateMicRefPathSplitterSecondOutput();
    kymerAdaptiveAnc_DisconnectFbcOutputs();
    kymerAdaptiveAnc_DisconnectMicRefPathSplitterOutputs();
}

/*! \brief Destroy AANC concurrency graph.
    \param transition - TRUE for concurrency graph destroy in transitional case
                      - FALSE for concurrency graph destroy in full disable case
    \return void
*/
static void kymeraAdaptiveAnc_DestroyConcurrency(bool transition)
{
    if (kymeraAdaptiveAnc_IsAancActive())
    {
        kymeraAdaptiveAnc_DestroyEchoCancellerChain();
        if(!transition)
        {
            /* Stop and destroy AANC chain during full disable case*/
            ChainStop(kymeraAdaptiveAnc_GetChain(CHAIN_AANC));
            kymeraAdaptiveAnc_DestroyAancChain();
        }
    }
}

/*! \brief Enable AANC standalone graph.
    \param transition - TRUE for standalone graph enable during transition from concurrency to standalone
                      - FALSE for standalone graph enable in full enable case
    \return void
*/
static void kymeraAdaptiveAnc_EnableStandalone(bool transition)
{
    DEBUG_LOG("KymeraAdaptiveAnc_EnableStandalone");
    appKymeraConfigureDspPowerMode();
    if (!Kymera_MicConnect(mic_user_aanc))
    {
        MessageSendLater(KymeraGetTask(), KYMERA_INTERNAL_MIC_CONNECTION_TIMEOUT_ANC,
                         NULL, MIC_CONNECT_RETRY_MS);
    }
    else
    {
        if(transition)
        {
            /* Enable Adaptivity during transitional case */
            kymeraAdaptiveAnc_UpdateAdaptivity(ENABLE_ADAPTIVITY);
        }
        else
        {
            /* start AANC chain in case of full enable */
            ChainStart(kymeraAdaptiveAnc_GetChain(CHAIN_AANC));
        }

        if(appKymeraGetState() == KYMERA_STATE_IDLE)
        {
            appKymeraSetState(KYMERA_STATE_ADAPTIVE_ANC_STARTED);
            appKymeraSetConcurrentState(KYMERA_CON_STATE_NONE);
        }
        if(appKymeraGetState() == KYMERA_STATE_TONE_PLAYING)
        {
            KymeraAdaptiveAnc_DisableAdaptivity();
            appKymeraSetConcurrentState(KYMERA_CON_STATE_WITH_ADAPTIVE_ANC);
        }
        /* Update optimum DSP clock for AANC usecase */
        appKymeraConfigureDspPowerMode();
    }
}

/*! \brief Destroy AANC concurrency graph.
    \param transition - TRUE for concurrency graph disable in transitional case
                      - FALSE for concurrency graph disable in full disable case
    \return void
*/
static void kymeraAdaptiveAnc_DisableStandalone(bool transition)
{
    DEBUG_LOG("KymeraAdaptiveAnc_DisableStandalone");
    if (kymeraAdaptiveAnc_IsAancActive())
    {
        if(transition)
        {
            /* Disable Adaptivity until concurrency chains are created during transitional case */
            kymeraAdaptiveAnc_UpdateAdaptivity(DISABLE_ADAPTIVITY);
            Kymera_MicDisconnect(mic_user_aanc);
        }
        else
        {
            /* Stop and destroy AANC chain during full disable case*/
            ChainStop(kymeraAdaptiveAnc_GetChain(CHAIN_AANC));
            Kymera_MicDisconnect(mic_user_aanc);
            kymeraAdaptiveAnc_DestroyAancChain();
        }
        appKymeraSetState(KYMERA_STATE_IDLE);
        appKymeraSetConcurrentState(KYMERA_CON_STATE_NONE);
        /* Update optimum DSP clock for AANC usecase */
        appKymeraConfigureDspPowerMode();
    }
}

static void kymeraAdaptiveAnc_EnableConcurrency(bool transition)
{
    DEBUG_LOG("KymeraAdaptiveAnc_EnableConcurrency");

    PanicFalse(kymeraAdaptiveAnc_IsAancActive());
    /* Boost DSP clock to turbo */
    appKymeraSetActiveDspClock(AUDIO_DSP_TURBO_CLOCK);

    kymeraAdaptiveAnc_CreateEchoCancellerChain();

    if (!Kymera_MicConnect(mic_user_aanc))
    {
        kymeraAdaptiveAnc_DestroyEchoCancellerChain();
        MessageSendLater(KymeraGetTask(), KYMERA_INTERNAL_MIC_CONNECTION_TIMEOUT_ANC,
                         NULL, MIC_CONNECT_RETRY_MS);
    }
    else
    {
        kymeraAdaptiveAnc_ConnectConcurrency();
        kymeraAdaptiveAnc_StartConcurrency(transition);
        if(kymeraAdaptiveAnc_IsVaSessionActive() && !kymeraAdapativeAnc_IsMusicActive())
            appKymeraSetState(KYMERA_STATE_ADAPTIVE_ANC_STARTED);
        appKymeraSetConcurrentState(KYMERA_CON_STATE_WITH_ADAPTIVE_ANC);
        /* Update optimum DSP clock for AANC usecase */
        appKymeraConfigureDspPowerMode();
    }
}

static void kymeraAdaptiveAnc_DisableConcurrency(bool transition)
{
    DEBUG_LOG("KymeraAdaptiveAnc_DisableConcurrency");
    if (kymeraAdaptiveAnc_IsAancActive())
    {
        kymeraAdaptiveAnc_StopConcurrency();

        kymeraAdaptiveAnc_DisconnectConcurrency();
        Kymera_MicDisconnect(mic_user_aanc);

        kymeraAdaptiveAnc_DestroyConcurrency(transition);
        appKymeraSetConcurrentState(KYMERA_CON_STATE_NONE);
        if(kymeraAdaptiveAnc_IsVaSessionActive() && !kymeraAdapativeAnc_IsMusicActive())
            appKymeraSetState(KYMERA_STATE_IDLE);
        /* Update optimum DSP clock for AANC usecase */
        appKymeraConfigureDspPowerMode();
    }
}

void KymeraAdaptive_ConcurrencyToStandalone(void)
{
    if(!kymeraAdaptiveAnc_IsVaSessionActive())
    {
        kymeraAdaptiveAnc_DisableConcurrency(TRUE);
        kymeraAdaptiveAnc_EnableStandalone(TRUE);
    }
    else
    {
        /* VA session is active so do nothing */
    }
}

void KymeraAdaptive_StandaloneToConcurrency(void)
{
    if(!kymeraAdaptiveAnc_IsVaSessionActive())
    {
        kymeraAdaptiveAnc_DisableStandalone(TRUE);
        kymeraAdaptiveAnc_EnableConcurrency(TRUE);
    }
    else
    {
        /* Concurrency is active already so do nothing */
    }
}
#endif

void KymeraAdaptiveAnc_Enable(const KYMERA_INTERNAL_AANC_ENABLE_T* msg)
{
    /* To identify if enable request is from user
       or due to transition between standalone and concurrencies*/
    bool transition = (msg == NULL) ? TRUE : FALSE;

    /* Panic if enable request not from user or transitional case */
    if((msg == NULL) && (!kymeraAdaptiveAnc_IsAancActive()))
        Panic();

#ifdef INCLUDE_MIC_CONCURRENCY
    /* Create an AANC operator only when ANC has enabled by the user,
     * otherwise creation would be ignored as AANC operator already created in transitional case */
    if(msg)
    {
        kymeraAdaptiveAnc_CreateAancChain(msg, aanc_usecase_standalone);
    }

    if(kymeraAdapativeAnc_IsConcurrencyAudioSourceActive())
    {
        kymeraAdaptiveAnc_EnableConcurrency(transition);
    }
    else /* Idle or tones/prompts active */
    {
        kymeraAdaptiveAnc_EnableStandalone(transition);
    }
#else
    switch(appKymeraGetState())
    {
        case KYMERA_STATE_IDLE:
            if(Kymera_IsVaCaptureActive())
            {
                /*kymeraAdaptiveAnc_CreateWithConcurrency(msg);*//* TBD*/
            }
            else
            {
                kymerAdaptiveAnc_CreateStandalone(msg);
            }
            appKymeraSetState(KYMERA_STATE_ADAPTIVE_ANC_STARTED);
        break;

        case KYMERA_STATE_SCO_ACTIVE:
            kymeraAdaptiveAnc_EnableAancInScoActive(msg);
            break;

        case KYMERA_STATE_A2DP_STARTING_A:
        case KYMERA_STATE_A2DP_STARTING_B:
        case KYMERA_STATE_A2DP_STARTING_C:
        case KYMERA_STATE_A2DP_STARTING_SLAVE:
        case KYMERA_STATE_A2DP_STREAMING:
        case KYMERA_STATE_A2DP_STREAMING_WITH_FORWARDING:
            kymerAdaptiveAnc_CreateStandalone(msg);
            KymeraAdaptiveAnc_DisableAdaptivity();
            break;

        case KYMERA_STATE_TONE_PLAYING:
            kymerAdaptiveAnc_CreateStandalone(msg);
            KymeraAdaptiveAnc_DisableAdaptivity();
            break;

        default:
        break;
    }
#endif

}

void KymeraAdaptiveAnc_Disable(void)
{
    /* Assuming standalone Adaptive ANC for now */
    kymeraAdaptiveAnc_PanicIfNotActive();

#ifdef INCLUDE_MIC_CONCURRENCY
    if(kymeraAdapativeAnc_IsConcurrencyAudioSourceActive())
    {
        kymeraAdaptiveAnc_DisableConcurrency(FALSE);
    }
    else /* Idle or tones/prompts active */
    {
        kymeraAdaptiveAnc_DisableStandalone(FALSE);
    }
#else
    switch(appKymeraGetState())
    {
        case KYMERA_STATE_ADAPTIVE_ANC_STARTED:
                if(Kymera_IsVaCaptureActive())
                {
                   /*kymerAdaptiveAnc_DestroyAndDisconnect_ForConcurrency();*//*TBD*/
                }
                else
                {
                   kymerAdaptiveAnc_DestroyAndDisconnectStandalone();
                }
                appKymeraSetState(KYMERA_STATE_IDLE);
                break;

        case KYMERA_STATE_SCO_ACTIVE:
                kymeraAdaptiveAnc_DisableAancInScoActive();
                break;

        case KYMERA_STATE_A2DP_STREAMING:
        case KYMERA_STATE_A2DP_STREAMING_WITH_FORWARDING:

                kymerAdaptiveAnc_DestroyAndDisconnectStandalone();
                break;
        default:
            break;
    }
#endif
}

#ifndef INCLUDE_MIC_CONCURRENCY
void KymeraAdaptiveAnc_ReconfigureAancOnScoStop(void)
{
    if(kymeraAdaptiveAnc_IsAancActive())
    {       
        DEBUG_LOG("KymeraAdaptiveAnc_ReconfigureAancOnScoStop to Standalone");

        /*If in Standby destroy chain, as user has not enabled ANC in SCO call*/
        if (AncStateManager_IsEnabled())
        {
            /*If user had enabled ANC, then reconfigure after SCO call*/
            /*Concurrency use-case is done, so reconfigure AEC ref for standalone*/
            KymeraAdaptiveAnc_ConfigureAec();
            ChainStart(kymeraAdaptiveAnc_GetChain(CHAIN_AANC));

            /* Its possible that A2DP streaming, like VA response */
            if(!appKymeraIsBusyStreaming())
                appKymeraSetState(KYMERA_STATE_ADAPTIVE_ANC_STARTED);
        }
        else
        {
            /*Destroy AANC chain*/
            kymerAdaptiveAnc_DestroyAndDisconnectStandalone();
        }
    }
}

/*! \brief If Adaptive ANC is defined in the project, then SCO should work with Adaptive ANC.
And for that, we require SCO Rx path to be connected to splitter such that it feeds to both
Adaptive ANC capability (for silence detection) & AEC Reference.
On the same lines, since Adaptive ANC configures AEC reference input mics to 16KHz, if SCO
had negotiated NB, then we require a down-sampler between AEC reference and SCO Tx path 
*/
void KymeraAdaptiveAnc_CreateScoAancConcurrencyChains(aec_connect_audio_input_t *aec_input_param,
                                                                                                aec_connect_audio_output_t *aec_output_param,
                                                                                                Source mic_source_to_sync_with, 
                                                                                                appKymeraScoMode sco_mode)
{
    kymera_chain_handle_t sco_chain = PanicNull(appKymeraGetScoChain());

    uint8 nr_mics = Kymera_GetNumberOfMics();
    DEBUG_LOG("KymeraAdaptiveAnc_CreateScoAancConcurrencyChains");
    kymeraAdaptiveAnc_CreateChainForConcurrency();
    kymeraAdaptiveAnc_ConnectScoReceive(ChainGetOutput(sco_chain, EPR_SCO_VOL_OUT), sco_mode);

    /* If NB is the negotiated codec and then if Adaptive ANC is enabled in build then we need to have AEC output
        connect to down-sampler */
    if (sco_mode == SCO_NB)
    {
        adaptive_anc_sco_send_t sco_send = {0};

        sco_send.cVc_in1 = ChainGetInput(sco_chain, EPR_CVC_SEND_IN1);
        sco_send.cVc_in2 = (nr_mics > 1) ? ChainGetInput(sco_chain, EPR_CVC_SEND_IN2) : NULL;
        sco_send.cVc_ref_in = ChainGetInput(sco_chain, EPR_CVC_SEND_REF_IN);

        /*Get the re-sampler input to be connected with AEC ref and re-sampler output to connect to cVc input */
        kymeraAdaptiveAnc_ConnectScoSend(&sco_send);

        if (kymeraAdaptiveAnc_GetInputForRole(EPR_IIR_16K_IN1) != NULL)
        {
            aec_input_param->mic_output_1 = kymeraAdaptiveAnc_GetInputForRole(EPR_IIR_16K_IN1);
            aec_input_param->mic_output_2 = (nr_mics > 1) ? kymeraAdaptiveAnc_GetInputForRole(EPR_IIR_16K_IN2) : NULL;/*TBD include 3 rd SCO mic*/
            aec_input_param->reference_output = kymeraAdaptiveAnc_GetInputForRole(EPR_IIR_AEC_REF_16K_IN);
        }
    }
    
    /*Configure AANC mics concurrently, if AANC is supported in build*/
    /*Please note this function modifies aec_input_param mic input and output based on mic configurations*/
    kymeraAdaptiveAnc_ConfigureMicsForConcurrency(aec_input_param, mic_source_to_sync_with);

    /* SCO Rx path is split with AEC Ref & Adaptive ANC */
    if(kymeraAdaptiveAnc_GetOutputForRole(EPR_SPLT_AEC_OUT) != NULL)
        aec_output_param->input_1 = kymeraAdaptiveAnc_GetOutputForRole(EPR_SPLT_AEC_OUT);
}


void KymeraAdaptiveAnc_StartScoAancConcurrencyChains(appKymeraScoMode sco_mode)
{
    DEBUG_LOG("KymeraAdaptiveAnc_StartScoAancConcurrencyChains");
    kymeraAdaptiveAnc_PanicIfNotActive();

    /*Connect SCO chains with new operators if AANC is present*/
    /*For shared mics with AANC, connect the splitter output to SCO Tx path.
        This is done after AEC input is stream connected such that splitter input is configured*/
    if (sco_mode == SCO_WB)
    {
        kymeraScoConnectSplitterOutputToCvcSend();
    }
    else if (sco_mode == SCO_NB)
    {
        kymeraAdaptiveAnc_ConnectSplitterOutputToScoTxResampler();
    }        
    
    /*Stream connect Splitter input and output*/
    kymeraAdaptiveAnc_ConnectTxSplitterToAanc();
    kymeraAdaptiveAnc_ConnectRxSplitterToAanc();
    
    /*Cannot delay starting of FF path. This needs to be in sync with the FB path coming from AEC ref*/
    kymeraAdaptiveAnc_ActivateTxSplitterSecondOutput();
    
    /*Activate the Rx splitter second output*/
    kymeraAdaptiveAnc_ActivateRxSplitterSecondOutput();

    /*Start AANC chain*/
    ChainStart(kymeraAdaptiveAnc_GetChain(CHAIN_AANC));

    /*Start Splitter Chains*/
    kymeraAdaptiveAnc_StartScoSendReceive();
}

void KymeraAdaptiveAnc_DisconnectSco(appKymeraScoMode sco_mode)
{
    if(kymeraAdaptiveAnc_GetChain(CHAIN_RX_SPLITTER))
    {          
        kymeraAdaptiveAnc_DeactivateRxSplitterSecondOutput();
        ChainStop(kymeraAdaptiveAnc_GetChain(CHAIN_RX_SPLITTER));
        StreamDisconnect(NULL, kymeraAdaptiveAnc_GetInput(CHAIN_RX_SPLITTER, EPR_SPLT_SCO_IN));
        StreamDisconnect(kymeraAdaptiveAnc_GetOutput(CHAIN_RX_SPLITTER, EPR_SPLT_AEC_OUT), NULL);

        if(kymeraAdaptiveAnc_IsAancActive())
        {
            StreamDisconnect(kymeraAdaptiveAnc_GetOutput(CHAIN_RX_SPLITTER, EPR_SPLT_OUT_ANC_VAD), NULL);
        }
    }
    
    if(kymeraAdaptiveAnc_GetChain(CHAIN_RX_RESAMPLER))
    {
        kymeraAdaptiveAnc_DestroyRxResamplerChain();
    }

    if(kymeraAdaptiveAnc_GetChain(CHAIN_TX_SPLITTER))
    {
        kymeraAdaptiveAnc_DeactivateTxSplitterSecondOutput();
        ChainStop(kymeraAdaptiveAnc_GetChain(CHAIN_TX_SPLITTER));
        
        StreamDisconnect(NULL, kymeraAdaptiveAnc_GetInput(CHAIN_TX_SPLITTER, EPR_AANC_BASIC_PASS_IN));
        StreamDisconnect(kymeraAdaptiveAnc_GetOutput(CHAIN_TX_SPLITTER, EPR_SPLT_OUT_CVC_SEND), NULL);

        if(kymeraAdaptiveAnc_IsAancActive())
        {
            StreamDisconnect(kymeraAdaptiveAnc_GetOutput(CHAIN_TX_SPLITTER, EPR_SPLT_OUT_ANC_FF), NULL);
        }
    }

    if (kymeraAdaptiveAnc_IsAancActive())
    {
        ChainStop(kymeraAdaptiveAnc_GetChain(CHAIN_AANC));
        kymerAdaptiveAnc_DisconnectMic_ForConcurrency();
    }

    if(sco_mode == SCO_NB)
    {
        kymeraAdaptiveAnc_DisconnectScoSend();
    }
    
    kymeraAdaptiveAnc_DestroySplitter();
}

#else
void KymeraAdaptiveAnc_UpdateKymeraStateToStandalone(void)
{
    if(kymeraAdaptiveAnc_IsAancActive())
    {       
        DEBUG_LOG("KymeraAdaptiveAnc_UpdateKymeraState to Standalone");
        if(!appKymeraIsBusyStreaming())
        {
            appKymeraSetState(KYMERA_STATE_ADAPTIVE_ANC_STARTED);
            appKymeraSetConcurrentState(KYMERA_CON_STATE_NONE);
        }
    }
}
#endif

/************************************General Public Utility functions*****************************************************/
void KymeraAdaptiveAnc_EnableGentleMute(void)
{
    Operator op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_AANC), OPR_AANC);
    if(op)
    {
        DEBUG_LOG("KymeraAdaptiveAnc_EnableGentleMute");
        OperatorsAdaptiveAncSetGentleMuteTimer(op, CONVERT_MSEC_TO_SEC(KYMERA_CONFIG_ANC_GENTLE_MUTE_TIMER));
        OperatorsAdaptiveAncSetModeOverrideCtrl(op, adaptive_anc_mode_gentle_mute);
    }
}

void KymeraAdaptiveAnc_ApplyModeChange(anc_mode_t new_mode, audio_anc_path_id feedforward_anc_path, adaptive_anc_hw_channel_t hw_channel)
{
    Operator op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_AANC), OPR_AANC);
    DEBUG_LOG("KymeraAdaptiveAnc_ApplyModeChange for mode %d", new_mode+1);

    if(op)
    {       
        kymeraAdaptiveAnc_SetControlPlantModel(op, feedforward_anc_path, hw_channel);
        kymeraAdaptiveAnc_SetStaticGain(op, feedforward_anc_path, hw_channel);

        if (new_mode==anc_mode_1)
        {
            OperatorsAdaptiveAncSetModeOverrideCtrl(op, adaptive_anc_mode_full);
            DEBUG_LOG("AANC changes mode to Full Proc");
        }
        else /*Other mode go into Static for now*/
        {
            OperatorsAdaptiveAncSetModeOverrideCtrl(op, adaptive_anc_mode_static);
            DEBUG_LOG("AANC changes mode to Static");
        }
    }
}

void KymeraAdaptiveAnc_SetUcid(anc_mode_t mode)
{
    Operator op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_AANC), OPR_AANC);

    if(op)
    {          
        DEBUG_LOG("KymeraAdaptiveAnc_SetUcid for mode %d", mode+1);
        OperatorsStandardSetUCID(op, mode); /*Mapping mode to UCID*/
    }
}

void KymeraAdaptiveAnc_UpdateInEarStatus(void)
{
    kymeraAdaptiveAnc_PanicIfNotActive();
    kymeraAdaptiveAnc_UpdateInEarStatus(IN_EAR);
}

void KymeraAdaptiveAnc_UpdateOutOfEarStatus(void)
{
    kymeraAdaptiveAnc_PanicIfNotActive();
    kymeraAdaptiveAnc_UpdateInEarStatus(OUT_OF_EAR);
}

void KymeraAdaptiveAnc_EnableAdaptivity(void)
{
    if(kymeraAdaptiveAnc_IsAancActive())
    {
        DEBUG_LOG("KymeraAdaptiveAnc_EnableAdaptivity");
        kymeraAdaptiveAnc_UpdateAdaptivity(ENABLE_ADAPTIVITY);
        /* Possible that A2DP/Tone which disabled adaptivity is now re-enabling it */
        if(appKymeraGetState() == KYMERA_STATE_IDLE)
            appKymeraSetState(KYMERA_STATE_ADAPTIVE_ANC_STARTED);
    }
}

void KymeraAdaptiveAnc_DisableAdaptivity(void)
{
    if(kymeraAdaptiveAnc_IsAancActive())
    {
        DEBUG_LOG("KymeraAdaptiveAnc_DisableAdaptivity");
        kymeraAdaptiveAnc_PanicIfNotActive();
        kymeraAdaptiveAnc_UpdateAdaptivity(DISABLE_ADAPTIVITY);
    }
}

/*Pre setup for SCO call if AANC already enabled*/
/*Pre setup for SCO call if AANC already enabled*/
void KymeraAdaptiveAnc_StopAancChainAndDisconnectAec(void)
{
    kymeraAdaptiveAnc_PanicIfNotActive();
    ChainStop(kymeraAdaptiveAnc_GetChain(CHAIN_AANC));
    
    Kymera_DisconnectAudioInputFromAec();

    Kymera_CloseMicrophone(appConfigAncFeedForwardMic(), non_exclusive_user);
    Kymera_CloseMicrophone(appConfigAncFeedBackMic(), non_exclusive_user);
}

void KymeraAdaptiveAnc_GetFFGain(uint8* gain)
{
    PanicNull(gain);
    kymeraAdaptiveAnc_PanicIfNotActive();
    kymeraAdaptiveAnc_GetGain(gain);
}

void KymeraAdaptiveAnc_SetGainValues(uint32 mantissa, uint32 exponent)
{
    kymeraAdaptiveAnc_PanicIfNotActive();
    kymeraAdaptiveAnc_SetGainValues(mantissa, exponent);
}

void KymeraAdaptiveAnc_EnableQuietMode(void)
{
    DEBUG_LOG_FN_ENTRY("KymeraAdaptiveAnc_EnableQuietMode");
    Operator op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_AANC), OPR_AANC);
    if(op)
    {
        OperatorsAdaptiveAncSetModeOverrideCtrl(op, adaptive_anc_mode_quiet);
    }
}

void KymeraAdaptiveAnc_DisableQuietMode(void)
{
    DEBUG_LOG_FN_ENTRY("KymeraAdaptiveAnc_DisableQuietMode");
    Operator op = ChainGetOperatorByRole(kymeraAdaptiveAnc_GetChain(CHAIN_AANC), OPR_AANC);
    if(op)
    {
        OperatorsAdaptiveAncSetModeOverrideCtrl(op, adaptive_anc_mode_full);
    }
}

bool KymeraAdaptiveAnc_ObtainCurrentAancMode(adaptive_anc_mode_t *aanc_mode)
{
    PanicNull(aanc_mode);
    kymeraAdaptiveAnc_PanicIfNotActive();
    kymeraAdaptiveAnc_GetCurrentMode(aanc_mode);
    return TRUE;
}

bool KymeraAdapativeAnc_IsNoiseLevelBelowQmThreshold(void)
{
    kymeraAdaptiveAnc_PanicIfNotActive();
    return kymeraAdaptiveAnc_IsNoiseLevelBelowQuietModeThreshold();
}

/**************** Utility functions for Adaptive ANC Tuning ********************************/
static audio_anc_path_id kymeraAdaptiveAnc_GetAncPath(void)
{
    audio_anc_path_id path=AUDIO_ANC_PATH_ID_NONE;

    /* Since Adaptive ANC is only supported on earbud application for now, checking for just 'left only' configurations*/
    if ((appConfigAncPathEnable() == feed_forward_mode_left_only) || (appConfigAncPathEnable() == feed_back_mode_left_only))
    {
        path = AUDIO_ANC_PATH_ID_FFA;
    }
    else if (appConfigAncPathEnable() == hybrid_mode_left_only)
    {
        path = AUDIO_ANC_PATH_ID_FFB;
    }
    else
    {
        DEBUG_LOG_ERROR("Adaptive ANC is supported only on left_only configurations");
    }

    return path;
}

static void kymeraAdaptiveAnc_GetMics(microphone_number_t *mic0, microphone_number_t *mic1)
{
    *mic0 = microphone_none;
    *mic1 = microphone_none;

    if (appConfigAncPathEnable() & hybrid_mode_left_only)
    {
        *mic0 = appConfigAncFeedForwardMic();
        *mic1 = appConfigAncFeedBackMic();
    }
}

static void kymeraAdaptiveAnc_TuningCreateOperators(void)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();

    const char aanc_tuning_edkcs[] = "download_aanc.edkcs";
    FILE_INDEX index = FileFind(FILE_ROOT, aanc_tuning_edkcs, strlen(aanc_tuning_edkcs));
    PanicFalse(index != FILE_NONE);
    theKymera->aanc_tuning_bundle_id = PanicZero (OperatorBundleLoad(index, 0)); /* 0 is processor ID */

#ifdef DOWNLOAD_USB_AUDIO
    const char usb_audio_edkcs[] = "download_usb_audio.edkcs";
    index = FileFind(FILE_ROOT, usb_audio_edkcs, strlen(usb_audio_edkcs));
    PanicFalse(index != FILE_NONE);
    theKymera->usb_audio_bundle_id = PanicZero (OperatorBundleLoad(index, 0)); /* 0 is processor ID */
#endif

    /* Create usb rx operator */
    theKymera->usb_rx = (Operator)(VmalOperatorCreate(EB_CAP_ID_USB_AUDIO_RX));
    PanicZero(theKymera->usb_rx);

    /* Create AANC tuning operator */
    theKymera->aanc_tuning = (Operator)(VmalOperatorCreate(CAP_ID_DOWNLOAD_AANC_TUNING));
    PanicZero(theKymera->aanc_tuning);

    /* Create usb rx operator */
    theKymera->usb_tx = (Operator)(VmalOperatorCreate(EB_CAP_ID_USB_AUDIO_TX));
    PanicZero(theKymera->usb_tx);

}

static void kymeraAdaptiveAnc_TuningConfigureOperators(uint16 usb_rate)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();

    /* configurations for usb_rx operator */
    uint16 usb_rx_config[] =
    {
        OPMSG_USB_AUDIO_ID_SET_CONNECTION_CONFIG,
        0,              // data_format
        usb_rate / 25,  // sample_rate
        1,              // number_of_channels /* Mono audio will be sent to AANC capability */
        16,             // subframe_size
        16,             // subframe_resolution
    };

    /* configurations for usb_tx operator */
    uint16 usb_tx_config[] =
    {
        OPMSG_USB_AUDIO_ID_SET_CONNECTION_CONFIG,
        0,              // data_format
        usb_rate / 25,  // sample_rate
        2,              // number_of_channels
        16,             // subframe_size
        16,             // subframe_resolution
    };

    KYMERA_INTERNAL_AANC_ENABLE_T* param = PanicUnlessMalloc(sizeof( KYMERA_INTERNAL_AANC_ENABLE_T));

    /*Even though device needs to be incase to perform AANC tuning, in_ear param needs to be set to TRUE as AANC capability
      runs in full processing mode only when the device is in ear */
    param->in_ear = TRUE;
    param->control_path = kymeraAdaptiveAnc_GetAncPath();
    param->hw_channel = adaptive_anc_hw_channel_0;
    param->current_mode = anc_mode_1;

    /* Configure usb rx operator */
    PanicFalse(VmalOperatorMessage(theKymera->usb_rx,
                                   usb_rx_config, SIZEOF_OPERATOR_MESSAGE(usb_rx_config),
                                   NULL, 0));

    /* Configure AANC tuning operator */
    kymeraAdaptiveAnc_ConfigureAancChain(theKymera->aanc_tuning, param, aanc_usecase_default);

    /* Configure usb tx operator */
    PanicFalse(VmalOperatorMessage(theKymera->usb_tx,
                                   usb_tx_config, SIZEOF_OPERATOR_MESSAGE(usb_tx_config),
                                   NULL, 0));

    free(param);
}

static void kymeraAdaptiveAnc_ConnectMicsAndDacToTuningOperator(Source ext_mic, Source int_mic, Sink DAC)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();

    /* Connect microphones */
    PanicFalse(StreamConnect(ext_mic,
                             StreamSinkFromOperatorTerminal(theKymera->aanc_tuning, AANC_TUNING_SINK_EXT_MIC)));

    PanicFalse(StreamConnect(int_mic,
                             StreamSinkFromOperatorTerminal(theKymera->aanc_tuning, AANC_TUNING_SINK_INT_MIC)));

    /* Connect DAC */
    PanicFalse(StreamConnect(StreamSourceFromOperatorTerminal(theKymera->aanc_tuning, AANC_TUNING_SOURCE_DAC),
                             DAC));
}

static void kymeraAdaptiveAnc_ConnectUsbRxAndTxOperatorsToTuningOperator(void)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();

    /* Connect backend (USB) to AANC operator */
    PanicFalse(StreamConnect(StreamSourceFromOperatorTerminal(theKymera->usb_rx, 0),
                             StreamSinkFromOperatorTerminal(theKymera->aanc_tuning, AANC_TUNING_SINK_USB)));


    /* Forwards external mic data to USb Tx */
    PanicFalse(StreamConnect(StreamSourceFromOperatorTerminal(theKymera->aanc_tuning, AANC_TUNING_SOURCE_EXT_MIC),
                             StreamSinkFromOperatorTerminal(theKymera->usb_tx, 0)));

    /* Forwards internal mic data to USb Tx */
    PanicFalse(StreamConnect(StreamSourceFromOperatorTerminal(theKymera->aanc_tuning, AANC_TUNING_SOURCE_INT_MIC),
                             StreamSinkFromOperatorTerminal(theKymera->usb_tx, 1)));
}

static void kymeraAdaptiveAnc_ConnectUsbRxAndTxOperatorsToUsbEndpoints(void)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();

    /* Connect USB ISO in endpoint to USB Rx operator */
    PanicFalse(StreamConnect(StreamUsbEndPointSource(end_point_iso_in),
                             StreamSinkFromOperatorTerminal(theKymera->usb_rx, 0)));

    /* Connect USB Tx operator to USB ISO out endpoint */
    PanicFalse(StreamConnect(StreamSourceFromOperatorTerminal(theKymera->usb_tx, 0),
                             StreamUsbEndPointSink(end_point_iso_out)));
}

/**************** Interface functions for Adaptive ANC Tuning ********************************/
void kymeraAdaptiveAnc_EnterAdaptiveAncTuning(uint16 usb_rate)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();
    DEBUG_LOG_FN_ENTRY("kymeraAdaptiveAnc_EnterAdaptiveAncTuning");
    MAKE_KYMERA_MESSAGE(KYMERA_INTERNAL_ADAPTIVE_ANC_TUNING_START);
    message->usb_rate = usb_rate;
    if(theKymera->busy_lock)
    {
        MessageSendConditionally(&theKymera->task, KYMERA_INTERNAL_ADAPTIVE_ANC_TUNING_START, message, &theKymera->busy_lock);
    }
    else if(theKymera->state == KYMERA_STATE_TONE_PLAYING)
    {
        MessageSendLater(&theKymera->task, KYMERA_INTERNAL_ADAPTIVE_ANC_TUNING_START, message, AANC_TUNNING_START_DELAY);
    }
    else
    {
       MessageSend(&theKymera->task, KYMERA_INTERNAL_ADAPTIVE_ANC_TUNING_START, message);
    }
}

void kymeraAdaptiveAnc_ExitAdaptiveAncTuning(void)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();
    DEBUG_LOG_FN_ENTRY("kymeraAdaptiveAnc_ExitAdaptiveAncTuning");
    MessageSend(&theKymera->task, KYMERA_INTERNAL_ADAPTIVE_ANC_TUNING_STOP, NULL);
}

void kymeraAdaptiveAnc_CreateAdaptiveAncTuningChain(uint16 usb_rate)
{
    DEBUG_LOG_FN_ENTRY("kymeraAdaptiveAnc_CreateAdaptiveAncTuningChain usb_rate: %d", usb_rate);

    kymeraTaskData *theKymera = KymeraGetTaskData();

    theKymera->usb_rate = usb_rate;

    /* Only 16KHz supported */
    PanicFalse(usb_rate == 16000);

    /* Turn on audio subsystem */
    OperatorFrameworkEnable(1);

    /* Move to ANC tuning state, this prevents A2DP and HFP from using kymera */
    appKymeraSetState(KYMERA_STATE_ANC_TUNING);

    appKymeraConfigureDspPowerMode();

    microphone_number_t mic0, mic1;
    kymeraAdaptiveAnc_GetMics(&mic0, &mic1);

    Source ext_mic = Kymera_GetMicrophoneSource(mic0, NULL, theKymera->usb_rate, high_priority_user);
    Source int_mic = Kymera_GetMicrophoneSource(mic1, NULL, theKymera->usb_rate, high_priority_user);

    PanicFalse(SourceSynchronise(ext_mic,int_mic));

    /* Get the DAC output sink */
    Sink DAC = (Sink)PanicFalse(StreamAudioSink(AUDIO_HARDWARE_CODEC, AUDIO_INSTANCE_0, AUDIO_CHANNEL_A));
    PanicFalse(SinkConfigure(DAC, STREAM_CODEC_OUTPUT_RATE, usb_rate));

    /* Create usb_rx, aanc_tuning, usb_tx operators */
    kymeraAdaptiveAnc_TuningCreateOperators();

    /* Configure usb_rx, aanc_tuning, usb_tx operators */
    kymeraAdaptiveAnc_TuningConfigureOperators(usb_rate);

    /* Connect microphones and DAC to tuning operator */
    kymeraAdaptiveAnc_ConnectMicsAndDacToTuningOperator(ext_mic, int_mic, DAC);

    kymeraAdaptiveAnc_ConnectUsbRxAndTxOperatorsToTuningOperator();

    kymeraAdaptiveAnc_ConnectUsbRxAndTxOperatorsToUsbEndpoints();


    /* Start the operators */
    Operator op_list[] = {theKymera->usb_rx, theKymera->aanc_tuning, theKymera->usb_tx};
    PanicFalse(OperatorStartMultiple(3, op_list, NULL));

    /* Ensure audio amp is on */
    appKymeraExternalAmpControl(TRUE);

    /* Set kymera lock to prevent anything else using kymera */
    theKymera->lock = TRUE;
}

void kymeraAdaptiveAnc_DestroyAdaptiveAncTuningChain(void)
{
    DEBUG_LOG_FN_ENTRY("kymeraAdaptiveAnc_DestroyAdaptiveAncTuningChain");

    if(appKymeraGetState() == KYMERA_STATE_ANC_TUNING)
    {
        kymeraTaskData *theKymera = KymeraGetTaskData();

        /* Turn audio amp is off */
        appKymeraExternalAmpControl(FALSE);

        /* Stop the operators */
        Operator op_list[] = {theKymera->usb_rx, theKymera->aanc_tuning, theKymera->usb_tx};
        PanicFalse(OperatorStopMultiple(3, op_list, NULL));

        /* Disconnect USB ISO in endpoint */
        StreamDisconnect(StreamUsbEndPointSource(end_point_iso_in), 0);

        /* Disconnect USB ISO out endpoint */
        StreamDisconnect(0, StreamUsbEndPointSink(end_point_iso_out));

        /* Get the DAC output sink */
        Sink DAC = (Sink)PanicFalse(StreamAudioSink(AUDIO_HARDWARE_CODEC, AUDIO_INSTANCE_0, AUDIO_CHANNEL_A));

        microphone_number_t mic0, mic1;
        kymeraAdaptiveAnc_GetMics(&mic0, &mic1);
        Source ext_mic = Microphones_GetMicrophoneSource(mic0);
        Source int_mic = Microphones_GetMicrophoneSource(mic1);

        StreamDisconnect(ext_mic, 0);
        Kymera_CloseMicrophone(mic0, high_priority_user);
        StreamDisconnect(int_mic, 0);
        Kymera_CloseMicrophone(mic1, high_priority_user);

        /* Disconnect speaker */
        StreamDisconnect(0, DAC);

        /* Distroy operators */
        OperatorsDestroy(op_list, 3);

        /* Unload bundle */
        PanicFalse(OperatorBundleUnload(theKymera->aanc_tuning_bundle_id));
    #ifdef DOWNLOAD_USB_AUDIO
        PanicFalse(OperatorBundleUnload(theKymera->usb_audio_bundle_id));
    #endif

        /* Clear kymera lock and go back to idle state to allow other uses of kymera */
        theKymera->lock = FALSE;
        appKymeraSetState(KYMERA_STATE_IDLE);

        /* Reset DSP clock to default*/
        appKymeraConfigureDspPowerMode();

        /* Turn off audio subsystem */
        OperatorFrameworkEnable(0);
    }
}

void KymeraAdaptiveAnc_EnableAdaptivityPostTonePromptStop(void)
{
    KymeraAdaptiveAnc_EnableAdaptivity();
#ifdef INCLUDE_MIC_CONCURRENCY
        if(!kymeraAdapativeAnc_IsConcurrencyAudioSourceActive())
            appKymeraSetConcurrentState(KYMERA_CON_STATE_NONE);
#else
        appKymeraSetConcurrentState(KYMERA_CON_STATE_NONE);
#endif
}

void KymeraAdaptiveAnc_DisableAdaptivityPreTonePromptPlay(void)
{
    /* It could so happen that either standalone ANC or in concurrency with
         SCO/A2DP, and there is tone/prompty play */
    if(appKymeraAdaptiveAncStarted() || appKymeraInConcurrency())
    {
        KymeraAdaptiveAnc_DisableAdaptivity();
        /* Probably Adaptive ANC is active, so update the concurrent state */
        appKymeraSetConcurrentState(KYMERA_CON_STATE_WITH_ADAPTIVE_ANC);
    }
}
#endif /* ENABLE_ADAPTIVE_ANC */
