/*!
\copyright  Copyright (c) 2017 - 2020 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version    
\file       kymera_sco_fwd.c
\brief      Kymera SCO
*/

#include <vmal.h>

#include "kymera_config.h"
#include "kymera_private.h"
#include "kymera_aec.h"

#include <scofwd_profile.h>
#include <anc_state_manager.h>
#include "scofwd_profile_config.h"
#include "kymera_mic_if.h"
#include "kymera_output_if.h"

#if defined(KYMERA_SCO_USE_3MIC)
#define MAX_NUM_OF_MICS_SUPPORTED (3)
#elif defined (KYMERA_SCO_USE_2MIC)
#define MAX_NUM_OF_MICS_SUPPORTED (2)
#else
#define MAX_NUM_OF_MICS_SUPPORTED (1)
#endif

#ifdef INCLUDE_MIC_CONCURRENCY
static bool kymera_ScofwdMicGetConnectionParameters(microphone_number_t *mic_ids, Sink *mic_sinks, uint8 *num_of_mics, uint32 *sample_rate, Sink *aec_ref_sink);
static bool kymera_ScofwdMicDisconnectIndication(const mic_change_info_t *info);

static const mic_callbacks_t kymera_MicScofwdCallbacks =
{
    .MicGetConnectionParameters = kymera_ScofwdMicGetConnectionParameters,
    .MicDisconnectIndication = kymera_ScofwdMicDisconnectIndication,
};

static const microphone_number_t kymera_ScofwdMandatoryMicIds[MAX_NUM_OF_MICS_SUPPORTED] =
{
    microphone_none
};

static mic_user_state_t kymera_ScofwdMicState =
{
    mic_user_state_non_interruptible,
};

static const mic_registry_per_user_t kymera_MicScofwdRegistry =
{
    .user = mic_user_scofwd,
    .callbacks = &kymera_MicScofwdCallbacks,
    .mandatory_mic_ids = &kymera_ScofwdMandatoryMicIds[0],
    .num_of_mandatory_mics = 0,
    .mic_user_state = &kymera_ScofwdMicState,
};
#endif

/*! AEC RX buffer size for the forwarded sco stream. */
#define AEC_RX_BUFFER_SIZE_MS 15

static bool appKymeraScoFwdOutputConnect(output_connection_t type, const output_source_t *sources);
static void appKymeraScoFwdOutputDisconnect(output_connection_t type);

static const output_custom_chain_creator_t output_creator_info =
{
    .OutputConnect = appKymeraScoFwdOutputConnect,
    .OutputDisconnect = appKymeraScoFwdOutputDisconnect,
};

static const output_registry_entry_t output_info =
{
    .user = output_user_sco_fwd,
    .connection = output_connection_mono,
    .creator = &output_creator_info,
};

static Sink appKymeraScoFwdGetInput(unsigned input_role)
{
    return ChainGetInput(appKymeraGetScoChain(), input_role);
}

static bool appKymeraScoFwdConnectInput(Source source, unsigned input_role)
{
    return (StreamConnect(source, appKymeraScoFwdGetInput(input_role)) != NULL);
}

static void appKymeraScoFwdDisconnectInput(unsigned input_role)
{
    StreamDisconnect(NULL, appKymeraScoFwdGetInput(input_role));
}

static bool appKymeraScoFwdOutputConnect(output_connection_t type, const output_source_t *sources)
{
    switch(type)
    {
        case output_connection_aux:
            return appKymeraScoFwdConnectInput(sources->aux, EPR_VOLUME_AUX);
        default:
            return FALSE;
    }
}

static void appKymeraScoFwdOutputDisconnect(output_connection_t type)
{
    switch(type)
    {
        case output_connection_aux:
            appKymeraScoFwdDisconnectInput(EPR_VOLUME_AUX);
            break;
        default:
            Panic();
            break;
    }
}

static void appKymeraScoForwardingSetSwitchedPassthrough(switched_passthrough_states state)
{
    kymera_chain_handle_t sco_chain = appKymeraGetScoChain();

    Operator spc_op = (Operator)PanicZero(ChainGetOperatorByRole(sco_chain,
                                                                 OPR_SWITCHED_PASSTHROUGH_CONSUMER));
    appKymeraConfigureSpcMode(spc_op, state);
}

static void appKymeraMicForwardingSetSwitchedPassthrough(switched_passthrough_states state)
{
    kymera_chain_handle_t sco_chain = appKymeraGetScoChain();
    Operator spc_op = (Operator)PanicZero(ChainGetOperatorByRole(sco_chain,
                                                                 OPR_MICFWD_RECV_SPC));
    appKymeraConfigureSpcMode(spc_op, state);
}

#ifdef INCLUDE_MIC_CONCURRENCY

static void kymera_PopulateScofwdConnectParams(microphone_number_t *mic_ids, Sink *mic_sinks, uint8 num_mics, Sink *aec_ref_sink)
{
    kymera_chain_handle_t sco_chain = appKymeraGetScoChain();
    PanicZero(mic_ids);
    PanicZero(mic_sinks);
    PanicZero(aec_ref_sink);
    PanicFalse(num_mics <= 3);

    mic_ids[0] = appConfigMicVoice();
    mic_sinks[0] = ChainGetInput(sco_chain, EPR_CVC_SEND_IN1);
    if(num_mics > 1)
    {
        mic_ids[1] = appConfigMicExternal();
        mic_sinks[1] = ChainGetInput(sco_chain, EPR_CVC_SEND_IN2);
    }
    if(num_mics > 2)
    {
        mic_ids[2] = appConfigMicInternal();
        mic_sinks[2] = ChainGetInput(sco_chain, EPR_CVC_SEND_IN3);
    }
    aec_ref_sink[0] = ChainGetInput(sco_chain, EPR_CVC_SEND_REF_IN);
}

static bool kymera_ScofwdMicDisconnectIndication(const mic_change_info_t *info)
{
    UNUSED(info);
    DEBUG_LOG_ERROR("kymera_ScofwdMicDisconnectIndication: SCOfwd shouldn't have to get disconnected");
    Panic();
    return TRUE;
}

static bool kymera_ScofwdMicGetConnectionParameters(microphone_number_t *mic_ids, Sink *mic_sinks, uint8 *num_of_mics, uint32 *sample_rate, Sink *aec_ref_sink)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();
    DEBUG_LOG("kymera_ScofwdMicGetConnectionParameters");

    *sample_rate = theKymera->sco_info->rate;
    *num_of_mics = Kymera_GetNumberOfMics();
    kymera_PopulateScofwdConnectParams(mic_ids, mic_sinks, theKymera->sco_info->mic_cfg, aec_ref_sink);
    return TRUE;
}

#else
static void appKymeraScoConnectToAecChain(const aec_connect_audio_input_t* input, const aec_connect_audio_output_t* output, const aec_audio_config_t *config)
{
    Kymera_SetAecUseCase(aec_usecase_voice_call);
    /* Connect AEC Input */
    Kymera_ConnectAudioInputToAec(input, config);
    /* Connect AEC Ouput */
    Kymera_ConnectAudioOutputToAec(output, config);
}

static void appKymeraScoDisconnectFromAecChain(void)
{
    /* Disconnect AEC Input */
    Kymera_DisconnectAudioInputFromAec();

    /* Disconnect AEC Ouput */
    Kymera_DisconnectAudioOutputFromAec();

    /* Reset AEC_REF usecase */
    Kymera_SetAecUseCase(aec_usecase_default);
}
#endif

void appKymeraScoForwardingPause(bool pause)
{
    switch (appKymeraGetState())
    {
        case KYMERA_STATE_SCO_ACTIVE_WITH_FORWARDING:
        case KYMERA_STATE_SCO_SLAVE_ACTIVE:
        {
            kymeraTaskData *theKymera = KymeraGetTaskData();
            if (theKymera->sco_info->mic_fwd)
            {
                DEBUG_LOG("appKymeraScoForwardingPause, pause %u", pause);
                appKymeraScoForwardingSetSwitchedPassthrough(pause ? CONSUMER_MODE : PASSTHROUGH_MODE);
            }
            else
            {
                DEBUG_LOG("appKymeraScoForwardingPause, MIC forwarding not enabled");                
            }
        }        
        break;
        
        default:
        {
            DEBUG_LOG("appKymeraScoForwardingPause, pause %u, incorrect state %u",
                       pause, appKymeraGetState());
        }
        break;        
    }
}

void appKymeraHandleInternalScoForwardingStartTx(Sink forwarding_sink)
{
    UNUSED(forwarding_sink);
    kymeraTaskData *theKymera = KymeraGetTaskData();

    /* Ignore request if not in SCO active state, or chain doesn't support SCO forwarding */
    if ((appKymeraGetState() != KYMERA_STATE_SCO_ACTIVE) ||
        !theKymera->sco_info->sco_fwd)
    {
        DEBUG_LOG("appKymeraHandleInternalScoStartForwardingTx, failed, state %d, sco_fwd %u",
                   appKymeraGetState(), theKymera->sco_info->sco_fwd);
        return;
    }
    
    kymera_chain_handle_t sco_chain = appKymeraGetScoChain();   
    Source scofwd_ep_src = PanicNull(ChainGetOutput(sco_chain, EPR_SCOFWD_TX_OTA));
        
    DEBUG_LOG("appKymeraHandleInternalScoStartForwardingTx, sink %p, source %p, state %d",
               forwarding_sink, scofwd_ep_src, appKymeraGetState());

    PanicNotZero(theKymera->lock);

    /* Tell SCO forwarding what the source of SCO frames is and enable the
     * passthrough to give it the SCO frames. */
    ScoFwdInitScoPacketising(scofwd_ep_src);
    appKymeraScoForwardingSetSwitchedPassthrough(PASSTHROUGH_MODE);

    /* Setup microphone forwarding if chain support it */
    if (theKymera->sco_info->mic_fwd)
    {
        /* Tell SCO forwarding what the forwarded microphone data sink is */
        Sink micfwd_ep_snk = PanicNull(ChainGetInput(sco_chain, EPR_MICFWD_RX_OTA));
        ScoFwdNotifyIncomingMicSink(micfwd_ep_snk);

        /* Use the currently selected microphone */
        ScoFwdMicForwardingEnable(theKymera->mic == MIC_SELECTION_REMOTE);

        /* Setup the SPC to use the currently selected microphone
         * Will inform the peer to enable/disable MIC fowrading once the
         * peer MIC path has been setup.
        */
        appKymeraSelectSpcSwitchInput(appKymeraGetMicSwitch(), theKymera->mic);

        /* Put the microphone receive switched passthrough consumer into passthrough mode */
        appKymeraMicForwardingSetSwitchedPassthrough(PASSTHROUGH_MODE);
    }

    /* All done, so update kymera state */
    appKymeraSetState(KYMERA_STATE_SCO_ACTIVE_WITH_FORWARDING);
}

bool appKymeraHandleInternalScoForwardingStopTx(void)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();
    DEBUG_LOG("appKymeraHandleInternalScoStopForwardingTx, state %u", appKymeraGetState());

    /* Can't stop forwarding if it hasn't been started */
    if (appKymeraGetState() != KYMERA_STATE_SCO_ACTIVE_WITH_FORWARDING)
        return FALSE;

    if (theKymera->sco_info->sco_fwd)
    {   
        /* Put switched passthrough consumer into consume mode, so that no SCO frames are sent*/
        appKymeraScoForwardingSetSwitchedPassthrough(CONSUMER_MODE);
    
        /* Put the microphone receive switched passthrough consumer into consume mode */
        if (theKymera->sco_info->mic_fwd)
            appKymeraMicForwardingSetSwitchedPassthrough(CONSUMER_MODE);
    
        /* All done, so update kymera state and return indicating success */
        appKymeraSetState(KYMERA_STATE_SCO_ACTIVE);
    }

    /* Clean up local SCO forwarding master forwarding state */
    ScoFwdClearForwarding();
    if (appConfigMicForwardingEnabled())
    {
        ScoFwdClearForwardingReceive();
    }

    return TRUE;
}

bool appKymeraHandleInternalScoSlaveStart(Source link_source, const appKymeraScoChainInfo *info,
                                          int16 volume_in_db)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();
    aec_connect_audio_output_t aec_output_param = {0};
    aec_audio_config_t aec_config = {0};

    DEBUG_LOG("appKymeraHandleInternalScoSlaveStart, source 0x%x, rate %u, state %u",
               link_source, info->rate, appKymeraGetState());

    PanicNotZero(theKymera->lock);

    /* If there is a tone still playing at this point,
     * it must be an interruptible tone, so cut it off */
    appKymeraTonePromptStop();

    Kymera_LeakthroughStopChainIfRunning();

    PanicFalse(Kymera_OutputCreatingCustomChain(output_user_sco_fwd));

    /* Can't start voice chain if we're not idle */
    PanicFalse(appKymeraGetState() == KYMERA_STATE_IDLE);

    /* SCO chain must be destroyed if we get here */
    PanicNotNull(appKymeraGetScoChain());
    
    /* Move to SCO active state now, what ever happens we end up in this state
      (even if it's temporary) */
    appKymeraSetState(KYMERA_STATE_SCO_SLAVE_ACTIVE);

    /* Create appropriate SCO chain */
    appKymeraScoCreateChain(info);
    kymera_chain_handle_t sco_chain = PanicNull(appKymeraGetScoChain());

    /* Fill the AEC Ref Output params */
    aec_output_param.input_1 = ChainGetOutput(sco_chain, EPR_SCO_VOL_OUT);

    /* sample rate */
    aec_config.mic_sample_rate = theKymera->sco_info->rate;
    aec_config.spk_sample_rate = theKymera->sco_info->rate;
    aec_config.buffer_size = AEC_RX_BUFFER_SIZE_MS;
    aec_config.is_source_clock_same = TRUE;
    aec_config.ttp_delay = SFWD_TTP_DELAY_US;

#ifdef INCLUDE_MIC_CONCURRENCY
    /* Connect to Mic interface */
    if (!Kymera_MicConnect(mic_user_scofwd))
    {
        DEBUG_LOG_ERROR("appKymeraHandleInternalScoSlaveStart: Mic connection was not successful. Sco should always be prepared.");
        Panic();
    }
#else
    aec_connect_audio_input_t aec_input_param = {0};

    uint8 nr_mics = Kymera_GetNumberOfMics();
    Source mic_src_1a = Kymera_GetMicrophoneSource(appConfigMicVoice(), NULL, appKymeraGetOptimalMicSampleRate(theKymera->sco_info->rate), high_priority_user);
    Source mic_src_1b = Kymera_GetMicrophoneSource((nr_mics > 1) ? appConfigMicExternal() : microphone_none, mic_src_1a, appKymeraGetOptimalMicSampleRate(theKymera->sco_info->rate), high_priority_user);
    Source mic_src_1c = Kymera_GetMicrophoneSource((nr_mics > 2) ? appConfigMicInternal() : microphone_none, mic_src_1a, appKymeraGetOptimalMicSampleRate(theKymera->sco_info->rate), high_priority_user);

    /* Fill the AEC Ref Input params */
    aec_input_param.mic_input_1 = mic_src_1a;
    aec_input_param.mic_input_2 = mic_src_1b;
    aec_input_param.mic_input_3 = mic_src_1c;
    aec_input_param.mic_output_1 = ChainGetInput(sco_chain, EPR_CVC_SEND_IN1);
    aec_input_param.mic_output_2 = (theKymera->sco_info->mic_cfg > 1) ? ChainGetInput(sco_chain, EPR_CVC_SEND_IN2) : NULL;
    aec_input_param.mic_output_3 = (theKymera->sco_info->mic_cfg > 2) ? ChainGetInput(sco_chain, EPR_CVC_SEND_IN3) : NULL;
    aec_input_param.reference_output = ChainGetInput(sco_chain, EPR_CVC_SEND_REF_IN);

    /* Connect to AEC Reference chain */
    appKymeraScoConnectToAecChain(&aec_input_param, &aec_output_param, &aec_config);
#endif

    /* Get sources and sinks for chain endpoints */
    Source sco_ep_src  = ChainGetOutput(sco_chain, EPR_MICFWD_TX_OTA);
    Sink sco_ep_snk    = ChainGetInput(sco_chain, EPR_SCOFWD_RX_OTA);

    ScoFwdNotifyIncomingSink(sco_ep_snk);
    if (theKymera->sco_info->mic_fwd)
        ScoFwdInitMicPacketising(sco_ep_src);

    /* Set async WBS decoder bitpool and buffer size */
    Operator awbs_op = ChainGetOperatorByRole(sco_chain, OPR_SCOFWD_RECV);
    OperatorsAwbsSetBitpoolValue(awbs_op, SFWD_MSBC_BITPOOL, TRUE);
    OperatorsStandardSetBufferSize(awbs_op, SFWD_RECV_CHAIN_BUFFER_SIZE);

    Operator spc_op = ChainGetOperatorByRole(sco_chain, OPR_SWITCHED_PASSTHROUGH_CONSUMER);
    if (spc_op)
        appKymeraConfigureScoSpcDataFormat(spc_op, ADF_GENERIC_ENCODED);
    
    /* Set async WBS encoder bitpool if microphone forwarding is enabled */
    if (theKymera->sco_info->mic_fwd)
    {
        Operator micwbs_op = ChainGetOperatorByRole(sco_chain, OPR_MICFWD_SEND);
        OperatorsAwbsSetBitpoolValue(micwbs_op, SFWD_MSBC_BITPOOL, FALSE);
    }

    /* Connect chain */
    ChainConnect(sco_chain);

#ifdef INCLUDE_MIC_CONCURRENCY
    /* Connect AEC Ouput as last operator of the RX chain */
    Kymera_ConnectAudioOutputToAec(&aec_output_param, &aec_config);
#endif

    /* Enable external amplifier if required */
    appKymeraExternalAmpControl(TRUE);

    /* Start chain */
    if (ChainStartAttempt(sco_chain))
    {
        theKymera->output_rate = theKymera->sco_info->rate;
        appKymeraHandleInternalScoSetVolume(volume_in_db);

        if (theKymera->sco_info->mic_fwd)
        {
            /* Default to not forwarding the MIC. The primary will tell us when to start/stop using
               SFWD_OTA_MSG_MICFWD_START and SFWD_OTA_MSG_MICFWD_STOP mesages */

            DEBUG_LOG("appKymeraHandleInternalScoSlaveStart, not forwarding MIC data");
            appKymeraScoForwardingSetSwitchedPassthrough(CONSUMER_MODE);
        }

        Kymera_LeakthroughSetAecUseCase(aec_usecase_sco_leakthrough);
    }
    else
    {
        DEBUG_LOG("appKymeraHandleInternalScoSlaveStart, could not start chain");
        appKymeraHandleInternalScoSlaveStop();
        return FALSE;
    }
    return TRUE;
}

void appKymeraHandleInternalScoSlaveStop(void)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();

    DEBUG_LOG("appKymeraHandleInternalScoSlaveStop, state %u", appKymeraGetState());

    PanicNotZero(theKymera->lock);

    /* Should be in SCO slave active state */
    PanicFalse(appKymeraGetState() == KYMERA_STATE_SCO_SLAVE_ACTIVE);

    /* Get current SCO chain */
    kymera_chain_handle_t sco_chain = PanicNull(appKymeraGetScoChain());

    /* Disable AEC_REF sidetone path */
    Kymera_AecEnableSidetonePath(FALSE);

    /* A tone still playing at this point must be interruptable */
    appKymeraTonePromptStop();

    Kymera_OutputDestroyingCustomChain(output_user_sco_fwd);

    /* Stop chains */
    ChainStop(sco_chain);

#ifdef INCLUDE_MIC_CONCURRENCY
    /* Disconnect AEC from RX chain as first operator */
    Kymera_DisconnectAudioOutputFromAec();
    Kymera_MicDisconnect(mic_user_scofwd);
#else
    /* Disconnect from AEC reference chain */
    appKymeraScoDisconnectFromAecChain();

    uint8 nr_mics = Kymera_GetNumberOfMics();
    Kymera_CloseMicrophone(appConfigMicVoice(), high_priority_user);
    Kymera_CloseMicrophone((nr_mics > 1) ? appConfigMicExternal() : microphone_none, high_priority_user);
    Kymera_CloseMicrophone((nr_mics > 2) ? appConfigMicInternal() : microphone_none, high_priority_user);
#endif

    /* Destroy chains */
    appKymeraDestroyScoChain();
    sco_chain = NULL;

    /* Disable external amplifier if required */
    if (!AncStateManager_IsEnabled())
        appKymeraExternalAmpControl(FALSE);

    /* Update state variables */
    theKymera->output_rate = 0;
}

void Kymera_ScoForwardingInit(void)
{
    Kymera_OutputRegister(&output_info);
#ifdef INCLUDE_MIC_CONCURRENCY
    Kymera_MicRegisterUser(&kymera_MicScofwdRegistry);
#endif
}
