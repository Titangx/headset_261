/*!
\copyright  Copyright (c) 2020 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\file       kymera_output.c
\brief      Kymera module with audio output chain definitions

*/

#include <audio_output.h>
#include <cap_id_prim.h>
#include <vmal.h>

#include "kymera_output_private.h"
#include "kymera_private.h"
#include "kymera_aec.h"
#include "kymera_config.h"
#include "kymera_source_sync.h"

/*To identify if a splitter is needed after output chain to actiavte second DAC path, when ANC is enabled in earbud application */
#if defined(ENABLE_ENHANCED_ANC) && !defined(INCLUDE_KYMERA_AEC) && !defined(ENABLE_ADAPTIVE_ANC)
#define INCLUDE_OUTPUT_SPLITTER
#endif

/*!@} */
#define VOLUME_CONTROL_SET_AUX_TTP_VERSION_MSB 0x2
#define DEFAULT_AEC_REF_TERMINAL_BUFFER_SIZE 15

#define SPLITTER_TERMINAL_IN_0       0
#define SPLITTER_TERMINAL_OUT_0      0
#define SPLITTER_TERMINAL_OUT_1      1

/*! \brief The chain output channels */
typedef enum
{
    output_right_channel = 0,
    output_left_channel
}output_channel_t;

typedef struct
{
    Source source;
    chain_endpoint_role_t role;
} input_t;

typedef struct
{
    Source input_1;
    Source input_2;
} connect_audio_output_t;

static struct
{
    struct
    {
        uint32 main;
        uint32 auxiliary;
    } input_rates;
    struct
    {
        Operator main;
        Operator auxiliary;
    } resamplers;
    unsigned chain_include_aec:1;
    unsigned chain_started:1;
} state;

static chain_endpoint_role_t kymeraOutput_GetOuputRole(output_channel_t is_left);

#ifdef INCLUDE_OUTPUT_SPLITTER
static Operator output_splitter;

static void kymeraOutput_CreateSplitter(void)
{
    output_splitter = (Operator)(VmalOperatorCreate(CAP_ID_SPLITTER));
}

static void kymeraOutput_ConfigureSplitter(void)
{
    OperatorsSplitterSetWorkingMode(output_splitter, splitter_mode_clone_input);
    OperatorsSplitterEnableSecondOutput(output_splitter, FALSE);
    OperatorsSplitterSetDataFormat(output_splitter, operator_data_format_pcm);
}

static void kymeraOutput_ConnectSplitter(void)
{
    Source output_channel_left = ChainGetOutput(KymeraGetTaskData()->chain_output_handle, kymeraOutput_GetOuputRole(output_left_channel));
    PanicFalse(StreamConnect(output_channel_left,
                             StreamSinkFromOperatorTerminal(output_splitter, SPLITTER_TERMINAL_IN_0)));
}

static void kymeraOutput_ConnectOutputChainToSplitter(void)
{
    kymeraOutput_CreateSplitter();
    kymeraOutput_ConfigureSplitter();
    kymeraOutput_ConnectSplitter();
}

static void kymeraOutput_ConnectSplitterToAudioSink(void)
{
    Source splitter_out_0 = StreamSourceFromOperatorTerminal(output_splitter, SPLITTER_TERMINAL_OUT_0);
    Source splitter_out_1 = StreamSourceFromOperatorTerminal(output_splitter, SPLITTER_TERMINAL_OUT_1);

    Kymera_ConnectOutputSource(splitter_out_0, splitter_out_1, KymeraGetTaskData()->output_rate);
}

static void kymeraOutput_StartSplitter(void)
{
    PanicZero(output_splitter);
    OperatorsSplitterEnableSecondOutput(output_splitter, TRUE);
    PanicFalse(OperatorStartMultiple(1, &output_splitter, NULL));
}

static void KymeraOutput_StopAndDisconnectSplitterFromAudioSink(void)
{
    Source splitter_out_0 = StreamSourceFromOperatorTerminal(output_splitter, SPLITTER_TERMINAL_OUT_0);
    Source splitter_out_1 = StreamSourceFromOperatorTerminal(output_splitter, SPLITTER_TERMINAL_OUT_1);

    /* Stop the splitter operator */
    PanicFalse(OperatorStopMultiple(1, &output_splitter, NULL));

    Kymera_DisconnectIfValid(splitter_out_0, NULL);
    Kymera_DisconnectIfValid(splitter_out_1, NULL);
}

static void KymeraOutput_DestroySplitter(void)
{
    OperatorsDestroy(&output_splitter, 1);
}

static void KymeraOutput_DisconnectAndDestroySplitter(void)
{
    KymeraOutput_StopAndDisconnectSplitterFromAudioSink();
    KymeraOutput_DestroySplitter();
}
#endif  /* INCLUDE_OUTPUT_SPLITTER */

static void kymeraOutput_ConnectChainToAudioSink(const connect_audio_output_t *params)
{
    if (state.chain_include_aec)
    {
        aec_connect_audio_output_t aec_connect_params = {0};
        aec_audio_config_t config = {0};
        aec_connect_params.input_1 = params->input_1;
        aec_connect_params.input_2 = params->input_2;

        config.spk_sample_rate = KymeraGetTaskData()->output_rate;
        config.ttp_delay = VA_MIC_TTP_LATENCY;

#ifdef ENABLE_AEC_LEAKTHROUGH
        config.is_source_clock_same = TRUE; //Same clock source for speaker and mic path for AEC-leakthrough.
                                            //Should have no implication on normal aec operation.
        config.buffer_size = DEFAULT_AEC_REF_TERMINAL_BUFFER_SIZE;
#endif

        Kymera_ConnectAudioOutputToAec(&aec_connect_params, &config);
    }
    else
    {
#if defined(INCLUDE_OUTPUT_SPLITTER)
        UNUSED(params);
        kymeraOutput_ConnectSplitterToAudioSink();
#else
        Kymera_ConnectOutputSource(params->input_1,params->input_2,KymeraGetTaskData()->output_rate);
#endif
    }
}

static void kymeraOutput_DisconnectChainFromAudioSink(const connect_audio_output_t *params)
{
    if (state.chain_include_aec)
    {
        Kymera_DisconnectAudioOutputFromAec();
    }
    else
    {
#ifdef INCLUDE_OUTPUT_SPLITTER
        KymeraOutput_DisconnectAndDestroySplitter();
#endif
        Kymera_DisconnectIfValid(params->input_1, NULL);
        Kymera_DisconnectIfValid(params->input_2, NULL);
        AudioOutputDisconnect();
    }
}

static chain_endpoint_role_t kymeraOutput_GetOuputRole(output_channel_t is_left)
{
    chain_endpoint_role_t output_role;

    if(appConfigOutputIsStereo())
    {
        output_role = is_left ? EPR_SOURCE_STEREO_OUTPUT_L : EPR_SOURCE_STEREO_OUTPUT_R;
    }
    else
    {
        output_role = EPR_SOURCE_MIXER_OUT;
    }

    return output_role;
}

static void kymeraOutput_ConfigureOperators(kymera_chain_handle_t chain,
                                            const kymera_output_chain_config *config,
                                            bool is_stereo)
{
    Operator volume_op;
    bool input_buffer_set = FALSE;

    if (config->source_sync_input_buffer_size_samples)
    {
        /* Not all chains have a separate latency buffer operator but if present
           then set the buffer size. Source Sync version X.X allows its input
           buffer size to be set, so chains using that version of source sync
           typically do not have a seperate latency buffer and the source sync
           input buffer size is set instead in appKymeraConfigureSourceSync(). */
        Operator op;
        if (GET_OP_FROM_CHAIN(op, chain, OPR_LATENCY_BUFFER))
        {
            OperatorsStandardSetBufferSize(op, config->source_sync_input_buffer_size_samples);
            /* Mark buffer size as done */
            input_buffer_set = TRUE;
        }
    }
    appKymeraConfigureSourceSync(chain, config, !input_buffer_set, is_stereo);

    volume_op = ChainGetOperatorByRole(chain, OPR_VOLUME_CONTROL);
    OperatorsStandardSetSampleRate(volume_op, config->rate);
    appKymeraSetVolume(chain, VOLUME_MUTE_IN_DB);
    PanicFalse(Kymera_SetOperatorUcid(chain, OPR_VOLUME_CONTROL, UCID_VOLUME_CONTROL));
    PanicFalse(Kymera_SetOperatorUcid(chain, OPR_SOURCE_SYNC, UCID_SOURCE_SYNC));
}

static const chain_config_t * kymeraOutput_GetChainConfig(const kymera_output_chain_config *config)
{
#ifdef INCLUDE_FIXED_OUTPUT_CHAIN
    UNUSED(config);
    return Kymera_GetChainConfigs()->chain_output_fixed_config;
#else
    if (config->chain_exclude_basic_passthrough)
    {
        return Kymera_GetChainConfigs()->chain_output_volume_no_passthrough_config;
    }
    else
    {
        return Kymera_GetChainConfigs()->chain_output_volume_config;
    }
#endif
}

/*! \brief Create only the audio output - e.g. the DACs */
static void KymeraOutput_ConnectToSpeakerPath(void)
{
    connect_audio_output_t connect_params = {0};
    connect_params.input_1 = ChainGetOutput(KymeraGetTaskData()->chain_output_handle, kymeraOutput_GetOuputRole(output_left_channel));

    if(appConfigOutputIsStereo())
    {
        connect_params.input_2 = ChainGetOutput(KymeraGetTaskData()->chain_output_handle, kymeraOutput_GetOuputRole(output_right_channel));
    }

    kymeraOutput_ConnectChainToAudioSink(&connect_params);
}

static Sink kymeraOutput_GetInput(unsigned input_role)
{
    return ChainGetInput(KymeraGetTaskData()->chain_output_handle, input_role);
}

static bool kymeraOutput_IsAuxTtpSupported(capablity_version_t cap_version)
{
    return cap_version.version_msb >= VOLUME_CONTROL_SET_AUX_TTP_VERSION_MSB ? TRUE : FALSE;
}

static void kymeraOutput_SetOverallSampleRate(uint32 rate)
{
    KymeraGetTaskData()->output_rate = rate;
    state.input_rates.auxiliary = rate;
    state.input_rates.main = rate;
}

static Operator kymeraOutput_CreateResampler(uint32 input_rate, uint32 output_rate)
{
    Operator op = CustomOperatorCreate(CAP_ID_IIR_RESAMPLER, OPERATOR_PROCESSOR_ID_0, operator_priority_lowest, NULL);
    OperatorsConfigureResampler(op, input_rate, output_rate);
    DEBUG_LOG("kymeraOutput_CreateResampler: op_id=%d, in_rate=%d, out_rate=%d", op, input_rate, output_rate);
    return op;
}

static void kymeraOutput_StartResampler(Operator resampler)
{
    if (resampler != INVALID_OPERATOR)
    {
        PanicFalse(OperatorStart(resampler));
        DEBUG_LOG("kymeraOutput_StartResampler: op_id=%d", resampler);
    }
}

static void kymeraOutput_DestroyResampler(Operator *resampler)
{
    if (state.chain_started)
    {
        PanicFalse(OperatorStop(*resampler));
    }
    CustomOperatorDestroy(resampler, 1);
    DEBUG_LOG("kymeraOutput_DestroyResampler: op_id=%d", *resampler);
    *resampler = INVALID_OPERATOR;
}

static void kymeraOutput_StartInputResamplers(void)
{
    kymeraOutput_StartResampler(state.resamplers.main);
    kymeraOutput_StartResampler(state.resamplers.auxiliary);
}

static void kymeraOutput_ConnectInput(input_t *connections, unsigned connections_length, Operator *resampler, uint32 in_rate)
{
    PanicFalse(*resampler == INVALID_OPERATOR);
    if (in_rate != KymeraGetTaskData()->output_rate)
    {
        *resampler = kymeraOutput_CreateResampler(in_rate, KymeraGetTaskData()->output_rate);
        for(unsigned i = 0; i < connections_length; i++)
        {
            PanicNull(StreamConnect(connections[i].source, StreamSinkFromOperatorTerminal(*resampler, i)));
            connections[i].source = StreamSourceFromOperatorTerminal(*resampler, i);
        }
    }
    for(unsigned i = 0; i < connections_length; i++)
    {
        PanicFalse(ChainConnectInput(KymeraGetTaskData()->chain_output_handle, connections[i].source, connections[i].role));
    }
    if (state.chain_started)
    {
        kymeraOutput_StartResampler(*resampler);
    }
}

static void kymeraOutput_DisconnectInput(chain_endpoint_role_t *roles, unsigned roles_length, Operator *resampler)
{
    if (*resampler != INVALID_OPERATOR)
    {
        kymeraOutput_DestroyResampler(resampler);
    }
    for(unsigned i = 0; i < roles_length; i++)
    {
        StreamDisconnect(NULL, kymeraOutput_GetInput(roles[i]));
    }
}

bool KymeraOutput_MustAlwaysIncludeAec(void)
{
#if defined(INCLUDE_KYMERA_AEC) || defined(ENABLE_ADAPTIVE_ANC)
    return TRUE;
#else
    return FALSE;
#endif
}

void KymeraOutput_SetDefaultOutputChainConfig(kymera_output_chain_config *config,
                                              uint32 rate, unsigned kick_period,
                                              unsigned buffer_size)
{
    memset(config, 0, sizeof(*config));
    config->rate = rate;
    config->kick_period = kick_period;
    config->source_sync_input_buffer_size_samples = buffer_size;
    /* By default or for source sync version <=3.3 the output buffer needs to
       be able to hold at least SS_MAX_PERIOD worth  of audio (default = 2 *
       Kp), but be less than SS_MAX_LATENCY (5 * Kp). The recommendation is 2 Kp
       more than SS_MAX_PERIOD, so 4 * Kp. */
    appKymeraSetSourceSyncConfigOutputBufferSize(config, 4, 0);
}

void KymeraOutput_CreateOperators(const kymera_output_chain_config *config, bool is_stereo)
{
    if (KymeraOutput_MustAlwaysIncludeAec())
    {
        state.chain_include_aec = TRUE;
    }
    else
    {
        state.chain_include_aec = config->chain_include_aec;
    }

    DEBUG_LOG_FN_ENTRY("KymeraOutput_CreateOperators: include_aec=%d, is_stereo=%d", state.chain_include_aec, is_stereo);
	kymeraOutput_SetOverallSampleRate(config->rate);
    /* Setting leakthrough mic path sample rate same as speaker path sampling rate */
    Kymera_setLeakthroughMicSampleRate(config->rate);
    KymeraGetTaskData()->chain_output_handle = ChainCreate(kymeraOutput_GetChainConfig(config));
    kymeraOutput_ConfigureOperators(KymeraGetTaskData()->chain_output_handle, config, is_stereo);
    PanicFalse(OperatorsFrameworkSetKickPeriod(config->kick_period));
}

void KymeraOutput_ConnectChain(void)
{
    DEBUG_LOG_FN_ENTRY("KymeraOutput_ConnectChain");
    ChainConnect(KymeraGetTaskData()->chain_output_handle);
#ifdef INCLUDE_OUTPUT_SPLITTER
    kymeraOutput_ConnectOutputChainToSplitter();
#endif
    KymeraOutput_ConnectToSpeakerPath();
}

void KymeraOutput_DestroyChain(void)
{
    kymera_chain_handle_t chain = KymeraGetTaskData()->chain_output_handle;
	kymeraOutput_SetOverallSampleRate(0);
    Kymera_setLeakthroughMicSampleRate(DEFAULT_MIC_RATE);
    DEBUG_LOG_FN_ENTRY("KymeraOutput_DestroyChain");
    PanicNull(chain);

#ifdef INCLUDE_MIRRORING
    /* Destroying the output chain powers-off the DSP, if another
       prompt or activity is pending, the DSP has to start all over again
       which takes a long time. Therefore prospectively power on the DSP
       before destroying the output chain, which will avoid an unneccesary
       power-off/on
     */
    appKymeraProspectiveDspPowerOn();
#endif

    connect_audio_output_t disconnect_params = {0};
    disconnect_params.input_1 = ChainGetOutput(chain, kymeraOutput_GetOuputRole(output_left_channel));
    disconnect_params.input_2 = ChainGetOutput(chain, kymeraOutput_GetOuputRole(output_right_channel));
    ChainStop(chain);
    kymeraOutput_DisconnectChainFromAudioSink(&disconnect_params);
    state.chain_started = FALSE;

    ChainDestroy(chain);
    KymeraGetTaskData()->chain_output_handle = NULL;
    state.chain_include_aec = FALSE;
}

void KymeraOutput_ChainStart(void)
{
    if (state.chain_started == FALSE)
    {
        DEBUG_LOG_FN_ENTRY("KymeraOutput_ChainStart");
#ifdef INCLUDE_OUTPUT_SPLITTER
        kymeraOutput_StartSplitter();
#endif
        ChainStart(KymeraGetTaskData()->chain_output_handle);
        kymeraOutput_StartInputResamplers();
        state.chain_started = TRUE;
    }
}

kymera_chain_handle_t KymeraOutput_GetOutputHandle(void)
{
    return KymeraGetTaskData()->chain_output_handle;
}

void KymeraOutput_SetMainVolume(int16 volume_in_db)
{
    DEBUG_LOG_FN_ENTRY("KymeraOutput_SetMainVolume: db=%d", volume_in_db);
    appKymeraSetMainVolume(KymeraGetTaskData()->chain_output_handle, volume_in_db);
}

void KymeraOutput_SetAuxVolume(int16 volume_in_db)
{
    DEBUG_LOG_FN_ENTRY("KymeraOutput_SetAuxVolume: db=%d", volume_in_db);
    appKymeraSetAuxVolume(KymeraGetTaskData()->chain_output_handle, volume_in_db);
}

bool KymeraOutput_SetAuxTtp(uint32 time_to_play)
{
    Operator vol_op;
    if (GET_OP_FROM_CHAIN(vol_op, KymeraGetTaskData()->chain_output_handle, OPR_VOLUME_CONTROL))
    {
        if (kymeraOutput_IsAuxTtpSupported(OperatorGetCapabilityVersion(vol_op)))
        {
            OperatorsVolumeSetAuxTimeToPlay(vol_op, time_to_play,  0);
            return TRUE;
        }
    }
    return FALSE;
}

uint32 KymeraOutput_GetMainSampleRate(void)
{
    // Currently other modules create output chains as well (Voice Chains), in those cases the API should return the commonly used output rate
    return (state.input_rates.main) ? state.input_rates.main : KymeraGetTaskData()->output_rate;
}

uint32 KymeraOutput_GetAuxSampleRate(void)
{
    // Currently other modules create output chains as well (Voice Chains), in those cases the API should return the commonly used output rate
    return (state.input_rates.auxiliary) ? state.input_rates.auxiliary : KymeraGetTaskData()->output_rate;
}

void KymeraOutput_SetMainSampleRate(uint32 rate)
{
    // Can only set after chain creation
    PanicFalse(KymeraGetTaskData()->chain_output_handle != NULL);
    state.input_rates.main = rate;
}

void KymeraOutput_SetAuxSampleRate(uint32 rate)
{
    // Can only set after chain creation
    PanicFalse(KymeraGetTaskData()->chain_output_handle != NULL);
    state.input_rates.auxiliary = rate;
}

void KymeraOutput_ConnectToStereoMainInput(Source left, Source right)
{
    input_t connections[] =
    {
        {left, EPR_SINK_STEREO_MIXER_L},
        {right, EPR_SINK_STEREO_MIXER_R},
    };

    kymeraOutput_ConnectInput(connections, ARRAY_DIM(connections), &state.resamplers.main, KymeraOutput_GetMainSampleRate());
}

void KymeraOutput_ConnectToMonoMainInput(Source mono)
{
    input_t connections[] =
    {
        {mono, EPR_SINK_MIXER_MAIN_IN},
    };

    kymeraOutput_ConnectInput(connections, ARRAY_DIM(connections), &state.resamplers.main, KymeraOutput_GetMainSampleRate());
}

void KymeraOutput_ConnectToAuxInput(Source aux)
{
    input_t connections[] =
    {
        {aux, EPR_VOLUME_AUX},
    };

    kymeraOutput_ConnectInput(connections, ARRAY_DIM(connections), &state.resamplers.auxiliary, KymeraOutput_GetAuxSampleRate());
}

void KymeraOutput_DisconnectStereoMainInput(void)
{
    chain_endpoint_role_t in_roles[] =
    {
        EPR_SINK_STEREO_MIXER_L,
        EPR_SINK_STEREO_MIXER_R
    };

    kymeraOutput_DisconnectInput(in_roles, ARRAY_DIM(in_roles), &state.resamplers.main);
}

void KymeraOutput_DisconnectMonoMainInput(void)
{
    chain_endpoint_role_t in_roles[] =
    {
        EPR_SINK_MIXER_MAIN_IN
    };

    kymeraOutput_DisconnectInput(in_roles, ARRAY_DIM(in_roles), &state.resamplers.main);
}

void KymeraOutput_DisconnectAuxInput(void)
{
    chain_endpoint_role_t in_roles[] =
    {
        EPR_VOLUME_AUX
    };

    kymeraOutput_DisconnectInput(in_roles, ARRAY_DIM(in_roles), &state.resamplers.auxiliary);
}
