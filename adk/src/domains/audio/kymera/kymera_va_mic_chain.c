/*!
\copyright  Copyright (c) 2019-2020 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\file
\brief      Kymera module to handle VA mic chain

*/

#include "kymera_va_mic_chain.h"
#include "kymera_private.h"
#include "kymera_aec.h"
#include "kymera_va_common.h"
#include "kymera_config.h"

#define METADATA_REFRAMING_SIZE (384)

typedef void (* SourceFunction) (Source *array, unsigned length_of_array);
typedef void (*ChainInputFunction) (const Sink *mic_inputs, unsigned num_of_mics, Sink aec_ref_input);

static void kymera_ConfigureVad(Operator vad, const void *params);
static void kymera_ConfigureCvc(Operator cvc, const void *params);
static void kymera_ConfigureSplitter(Operator splitter, const void *params);
static void kymera_ConfigureBasicPass(Operator basic_pass, const void *params);

static const operator_config_map_t operator_config_map[] =
{
    {OPR_VAD,      kymera_ConfigureVad},
    {OPR_CVC_SEND, kymera_ConfigureCvc},
    {OPR_SPLITTER, kymera_ConfigureSplitter},
    {OPR_MIC_GAIN, kymera_ConfigureBasicPass}
};

static const microphone_number_t mic_map[MAX_NUM_OF_MICS_SUPPORTED] =
{
    appConfigVaMic1(),
    appConfigVaMic2(),
};

static const chain_endpoint_role_t mic_input_roles[MAX_NUM_OF_MICS_SUPPORTED] =
{
    EPR_VA_MIC_MIC1_IN,
    EPR_VA_MIC_MIC2_IN,
};

static const appKymeraVaMicChainTable *chain_config_map;
static kymera_chain_handle_t va_mic_chain = NULL;

static void kymera_ConfigureVad(Operator vad, const void *params)
{
    UNUSED(params);
    OperatorsStandardSetSampleRate(vad, Kymera_GetVaSampleRate());
}

static void kymera_ConfigureCvc(Operator cvc, const void *params)
{
    UNUSED(params);
    OperatorsStandardSetUCID(cvc, UCID_CVC_SEND_VA);
}

static uint32 kymera_DivideRoundingUp(uint32 dividend, uint32 divisor)
{
    if (dividend == 0)
        return 0;
    else
        return ((dividend - 1) / divisor) + 1;
}

static void kymera_ConfigureSplitter(Operator splitter, const void *params)
{
    const va_mic_chain_op_params_t *op_params = params;
    uint32 buffer_size = kymera_DivideRoundingUp(op_params->max_pre_roll_in_ms * Kymera_GetVaSampleRate(), 1000);
    OperatorsSplitterSetWorkingMode(splitter, splitter_mode_buffer_input);
#ifdef HAVE_SRAM
    OperatorsSplitterSetBufferLocation(splitter, splitter_buffer_location_sram);
#else
    OperatorsSplitterSetBufferLocation(splitter, splitter_buffer_location_internal);
#endif
    OperatorsSplitterSetPacking(splitter, splitter_packing_packed);
    OperatorsSplitterSetDataFormat(splitter, operator_data_format_pcm);
    OperatorsStandardSetBufferSize(splitter, buffer_size);
    OperatorsStandardSetSampleRate(splitter, Kymera_GetVaSampleRate());
    OperatorsSplitterSetMetadataReframing(splitter, splitter_reframing_enable, METADATA_REFRAMING_SIZE);
}

static void kymera_ConfigureBasicPass(Operator basic_pass, const void *params)
{
    UNUSED(params);
    OperatorsStandardSetUCID(basic_pass, UCID_PASS_VA);
    OperatorsSetPassthroughDataFormat(basic_pass, operator_data_format_pcm);
}

static bool kymera_AreEqualChainParams(const kymera_va_mic_chain_params_t *a, const kymera_va_mic_chain_params_t *b)
{
    return (a->wake_up_word_detection == b->wake_up_word_detection) &&
           (a->clear_voice_capture    == b->clear_voice_capture) &&
           (a->number_of_mics         == b->number_of_mics);
}

static const chain_config_t * kymera_GetChainConfig(const kymera_va_mic_chain_params_t *params)
{
    unsigned i;

    PanicFalse(chain_config_map != NULL);
    for(i = 0; i < chain_config_map->table_length; i++)
    {
        if (kymera_AreEqualChainParams(&chain_config_map->chain_table[i].chain_params, params))
        {
            return chain_config_map->chain_table[i].chain_config;
        }
    }

    return NULL;
}

static Operator kymera_GetChainOperator(unsigned operator_role)
{
    PanicNull(va_mic_chain);
    return ChainGetOperatorByRole(va_mic_chain, operator_role);
}

static Sink kymera_GetChainInput(unsigned input_role)
{
    PanicNull(va_mic_chain);
    return ChainGetInput(va_mic_chain, input_role);
}

static Source kymera_GetChainOutput(unsigned output_role)
{
    PanicNull(va_mic_chain);
    return ChainGetOutput(va_mic_chain, output_role);
}

static void kymera_CreateChain(const kymera_va_mic_chain_params_t *params)
{
    PanicNotNull(va_mic_chain);
    const chain_config_t *chain_config = kymera_GetChainConfig(params);

    if (chain_config)
    {
        va_mic_chain = PanicNull(ChainCreate(chain_config));
    }
    else
    {
        PANIC("kymera_CreateChain: No compatible chain configuration found!");
    }
}

static void kymera_ConfigureChain(const va_mic_chain_op_params_t *params)
{
    Kymera_ConfigureChain(va_mic_chain, operator_config_map, ARRAY_DIM(operator_config_map), params);
}

#ifndef INCLUDE_MIC_CONCURRENCY

static void kymera_PreserveSources(Source *array, unsigned length_of_array)
{
    PanicFalse(OperatorFrameworkPreserve(0, NULL, length_of_array, array, 0, NULL));
}

static void kymera_ReleaseSources(Source *array, unsigned length_of_array)
{
    PanicFalse(OperatorFrameworkRelease(0, NULL, length_of_array, array, 0, NULL));
}

static void kymera_RunOnConnectedMics(SourceFunction function)
{
    unsigned number_of_mics = 0;
    Source mics[MAX_NUM_OF_MICS_SUPPORTED] = {NULL};

    for(unsigned i = 0; i < MAX_NUM_OF_MICS_SUPPORTED; i++)
    {
        if (kymera_GetChainInput(mic_input_roles[i]))
        {
            mics[number_of_mics] = PanicNull(Microphones_GetMicrophoneSource(mic_map[i]));
            number_of_mics++;
        }
    }

    function(mics, number_of_mics);
}

static aec_usecase_t kymera_GetVoiceCaptureUsecase(void)
{
    aec_usecase_t aec_usecase=aec_usecase_voice_assistant;

    if(AecLeakthrough_IsLeakthroughEnabled())
    {
        aec_usecase = aec_usecase_va_leakthrough;
    }

    return aec_usecase;
}

static void kymera_PopulateAecConnectParams(uint8 num_of_mics, const Sink *mic_sinks, const Source *mic_sources,
                                            Sink aec_ref_sink, aec_connect_audio_input_t *aec_params)
{
    aec_params->reference_output = aec_ref_sink;

    switch (num_of_mics)
    {
        case 3:
            aec_params->mic_input_3 = mic_sources[2];
            aec_params->mic_output_3 = mic_sinks[2];
        // Fallthrough
        case 2:
            aec_params->mic_input_2 = mic_sources[1];
            aec_params->mic_output_2 = mic_sinks[1];
        // Fallthrough
        case 1:
            aec_params->mic_input_1 = mic_sources[0];
            aec_params->mic_output_1 = mic_sinks[0];
            break;
        default:
            DEBUG_LOG_ERROR("kymera_PopulateAecConnectParams: Unsupported number of mics = %d", num_of_mics);
            Panic();
    }
}

static void kymera_PopulateAecConfig(uint32 sample_rate, aec_audio_config_t *aec_config)
{
    aec_config->ttp_delay = VA_MIC_TTP_LATENCY;
    aec_config->mic_sample_rate = sample_rate;
}

static void kymera_ConnectToAec(const Sink *mic_inputs, const Source *mics, unsigned num_of_mics, Sink aec_ref_input)
{
    aec_connect_audio_input_t connect_params = {0};
    aec_audio_config_t config = {0};

    kymera_PopulateAecConnectParams(num_of_mics, mic_inputs, mics, aec_ref_input, &connect_params);
    kymera_PopulateAecConfig(Kymera_GetVaSampleRate(), &config);
    Kymera_SetAecUseCase(kymera_GetVoiceCaptureUsecase());
    Kymera_ConnectAudioInputToAec(&connect_params, &config);
}

static void kymera_DisconnectChainFromAec(void)
{
    Kymera_DisconnectAudioInputFromAec();
    Kymera_SetAecUseCase(aec_usecase_default);
}

#endif

static void kymera_ConnectToMics(const Sink *mic_inputs, unsigned num_of_mics, Sink aec_ref_input)
{
#ifdef INCLUDE_MIC_CONCURRENCY
    UNUSED(num_of_mics);
    UNUSED(aec_ref_input);
    UNUSED(mic_inputs);
    bool connection_successful;

    connection_successful = Kymera_MicConnect(mic_user_va);
    if (!connection_successful)
    {
        DEBUG_LOG_ERROR("kymera_ConnectToMics: VA Mic connection was not successful.");
        Panic();
    }

#else
    unsigned i;
    Source mics[MAX_NUM_OF_MICS_SUPPORTED] = {NULL};


    for(i = 0; i < num_of_mics; i++)
    {
        mics[i] = Kymera_GetMicrophoneSource(mic_map[i], mics[0], Kymera_GetVaSampleRate(), high_priority_user);
    }

    kymera_ConnectToAec(mic_inputs, mics, num_of_mics, aec_ref_input);
#endif
}

static void kymera_DisconnectFromMics(const Sink *mic_inputs, unsigned num_of_mics, Sink aec_ref_input)
{
#ifdef INCLUDE_MIC_CONCURRENCY
    UNUSED(mic_inputs);
    UNUSED(num_of_mics);
    UNUSED(aec_ref_input);
    Kymera_MicDisconnect(mic_user_va);
#else
    unsigned i;

    UNUSED(mic_inputs);
    UNUSED(aec_ref_input);
    kymera_DisconnectChainFromAec();
    for(i = 0; i < num_of_mics; i++)
    {
        Kymera_CloseMicrophone(mic_map[i], high_priority_user);
    }
#endif
}

static void kymera_RunOnChainInputs(ChainInputFunction function)
{
    unsigned num_of_mics;
    Sink mic_inputs[MAX_NUM_OF_MICS_SUPPORTED] = {NULL};
    Sink sink;

    for(num_of_mics = 0; num_of_mics < MAX_NUM_OF_MICS_SUPPORTED; num_of_mics++)
    {
        sink = kymera_GetChainInput(mic_input_roles[num_of_mics]);
        if (sink == NULL)
            break;
        mic_inputs[num_of_mics] = sink;
    }

    function(mic_inputs, num_of_mics, kymera_GetChainInput(EPR_VA_MIC_AEC_IN));
}

static void kymera_ConnectChain(void)
{
    kymera_RunOnChainInputs(kymera_ConnectToMics);
    ChainConnect(va_mic_chain);
}

static void kymera_DisconnectChain(void)
{
    StreamDisconnect(Kymera_GetVaMicChainEncodeOutput(), NULL);
    StreamDisconnect(Kymera_GetVaMicChainWuwOutput(), NULL);
    kymera_RunOnChainInputs(kymera_DisconnectFromMics);
}

static graph_manager_delegate_op_t kymera_GetOperatorsToDelegate(void)
{
    graph_manager_delegate_op_t ops = {INVALID_OPERATOR};

    ops.cvc = kymera_GetChainOperator(OPR_CVC_SEND);
    ops.splitter = kymera_GetChainOperator(OPR_SPLITTER);
    ops.vad = kymera_GetChainOperator(OPR_VAD);

    return ops;
}

static void kymera_RunUsingOperatorsToDelegate(OperatorFunction function)
{
    graph_manager_delegate_op_t delegate_ops = kymera_GetOperatorsToDelegate();
    Operator ops[] =
    {
        delegate_ops.cvc,
        delegate_ops.splitter,
        delegate_ops.vad,
    };

    function(ops, ARRAY_DIM(ops));
}

static void kymera_ChainSleep(Operator *array, unsigned length_of_array)
{
    operator_list_t operators_to_exclude = {array, length_of_array};
    ChainSleep(va_mic_chain, &operators_to_exclude);
}

static void kymera_ChainWake(Operator *array, unsigned length_of_array)
{
    operator_list_t operators_to_exclude = {array, length_of_array};
    ChainWake(va_mic_chain, &operators_to_exclude);
}

void Kymera_CreateVaMicChain(const va_mic_chain_create_params_t *params)
{
    PanicFalse(params != NULL);
    kymera_CreateChain(&params->chain_params);
    kymera_ConfigureChain(&params->operators_params);
    kymera_ConnectChain();
}

void Kymera_DestroyVaMicChain(void)
{
    PanicNull(va_mic_chain);
    kymera_DisconnectChain();
    ChainDestroy(va_mic_chain);
    va_mic_chain = NULL;
}

void Kymera_StartVaMicChain(void)
{
    ChainStart(va_mic_chain);
}

void Kymera_StopVaMicChain(void)
{
    ChainStop(va_mic_chain);
}

void Kymera_VaMicChainSleep(void)
{
#ifdef INCLUDE_MIC_CONCURRENCY
    Kymera_MicSleep(mic_user_va);
#else
    kymera_RunOnConnectedMics(kymera_PreserveSources);
    Kymera_AecSleep();
#endif

    kymera_RunUsingOperatorsToDelegate(kymera_ChainSleep);
}

void Kymera_VaMicChainWake(void)
{
    kymera_RunUsingOperatorsToDelegate(kymera_ChainWake);

#ifdef INCLUDE_MIC_CONCURRENCY
    Kymera_MicWake(mic_user_va);
#else
    Kymera_AecWake();
    kymera_RunOnConnectedMics(kymera_ReleaseSources);
#endif
}

void Kymera_VaMicChainStartGraphManagerDelegation(Operator graph_manager, Operator wuw_engine)
{
    graph_manager_delegate_op_t operators = kymera_GetOperatorsToDelegate();
    operators.wuw_engine = wuw_engine;
    OperatorsGraphManagerStartDelegation(graph_manager, &operators);
}

void Kymera_VaMicChainStopGraphManagerDelegation(Operator graph_manager, Operator wuw_engine)
{
    graph_manager_delegate_op_t operators = kymera_GetOperatorsToDelegate();
    operators.wuw_engine = wuw_engine;
    OperatorsGraphManagerStopDelegation(graph_manager, &operators);
}

void Kymera_ActivateVaMicChainEncodeOutputAfterTimestamp(uint32 start_timestamp)
{
    Operator splitter = kymera_GetChainOperator(OPR_SPLITTER);
    OperatorsSplitterActivateOutputStreamAfterTimestamp(splitter, start_timestamp, splitter_output_stream_1);
}

void Kymera_ActivateVaMicChainEncodeOutput(void)
{
    Operator splitter = kymera_GetChainOperator(OPR_SPLITTER);
    OperatorsSplitterActivateOutputStream(splitter, splitter_output_stream_1);
}

void Kymera_DeactivateVaMicChainEncodeOutput(void)
{
    Operator splitter = kymera_GetChainOperator(OPR_SPLITTER);
    OperatorsSplitterDeactivateOutputStream(splitter, splitter_output_stream_1);
}

void Kymera_BufferVaMicChainEncodeOutput(void)
{
    Operator splitter = kymera_GetChainOperator(OPR_SPLITTER);
    OperatorsSplitterBufferOutputStream(splitter, splitter_output_stream_1);
}

void Kymera_ActivateVaMicChainWuwOutput(void)
{
    Operator splitter = kymera_GetChainOperator(OPR_SPLITTER);
    OperatorsSplitterActivateOutputStream(splitter, splitter_output_stream_0);
}

void Kymera_DeactivateVaMicChainWuwOutput(void)
{
    Operator splitter = kymera_GetChainOperator(OPR_SPLITTER);
    OperatorsSplitterDeactivateOutputStream(splitter, splitter_output_stream_0);
}

Source Kymera_GetVaMicChainEncodeOutput(void)
{
    return kymera_GetChainOutput(EPR_VA_MIC_ENCODE_OUT);
}

Source Kymera_GetVaMicChainWuwOutput(void)
{
    return kymera_GetChainOutput(EPR_VA_MIC_WUW_OUT);
}

bool Kymera_IsVaMicChainSupported(const kymera_va_mic_chain_params_t *params)
{
    return (kymera_GetChainConfig(params) != NULL);
}

bool Kymera_GetVaMicChainMicConnectionParams(microphone_number_t *mic_ids, Sink *mic_sinks, uint8 *num_of_mics, uint32 *sample_rate, Sink *aec_ref_sink)
{
    Sink sink;

    for(*num_of_mics = 0; *num_of_mics < MAX_NUM_OF_MICS_SUPPORTED; (*num_of_mics)++)
    {
        sink = kymera_GetChainInput(mic_input_roles[*num_of_mics]);
        if (sink == NULL)
            break;
        mic_ids[*num_of_mics] = mic_map[*num_of_mics];
        mic_sinks[*num_of_mics] = sink;
    }

    *aec_ref_sink = kymera_GetChainInput(EPR_VA_MIC_AEC_IN);
    *sample_rate = Kymera_GetVaSampleRate();

    return TRUE;
}

void Kymera_SetVaMicChainTable(const appKymeraVaMicChainTable *chain_table)
{
    chain_config_map = chain_table;
}
