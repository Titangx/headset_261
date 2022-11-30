/*!
\copyright  Copyright (c) 2017 - 2020  Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version    
\file       kymera_prompts.c
\brief      Kymera tones / prompts
*/

#include <operators.h>

#include "kymera_private.h"
#include "kymera_config.h"
#include "kymera_aec.h"
#include "kymera_output_if.h"
#include "system_clock.h"

#define BUFFER_SIZE_FACTOR 4

#define PREPARE_FOR_PROMPT_TIMEOUT (1000)

/* Indicates the buffer size required for SBC-prompts / tone-generator */
#define PROMPT_TONE_OUTPUT_SIZE_SBC (256)

/* Indicates the max buffer size required if a resampler is used in the chain.
 * max ratio for main content of 96kHz to the tones of 8kHz */
#define PROMPT_TONE_RESAMPLER_FACTOR_MAX (12)

static bool appKymeraOutputDisconnectRequest(void)
{
    appKymeraTonePromptStop();
    return TRUE;
}

static const output_callbacks_t output_callbacks =
{
   .OutputDisconnectRequest = appKymeraOutputDisconnectRequest,
};

static const output_registry_entry_t output_info =
{
    .user = output_user_prompt,
    .connection = output_connection_aux,
    .assume_chain_compatibility = TRUE,
    .callbacks = &output_callbacks,
};

static enum
{
    kymera_tone_idle,
    kymera_tone_ready_tone,
    kymera_tone_ready_prompt,
    kymera_tone_playing
} kymera_tone_state = kymera_tone_idle;

static kymera_chain_handle_t kymera_GetTonePromptChain(void)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();
    return theKymera->chain_tone_handle;
}

/*! \brief Setup the prompt audio source.
    \param source The prompt audio source.
*/
static void appKymeraPromptSourceSetup(Source source)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();
    MessageStreamTaskFromSource(source, &theKymera->task);
}

/*! \brief Close the prompt audio source.
    \param source The prompt audio source.
*/
static void appKymeraPromptSourceClose(Source source)
{
    if (source)
    {
        MessageStreamTaskFromSource(source, NULL);
        StreamDisconnect(source, NULL);
        SourceClose(source);
    }
}

static void kymera_PrepareOutputChain(uint16 sample_rate)
{
    kymera_output_chain_config config = {0};
    KymeraOutput_SetDefaultOutputChainConfig(&config, sample_rate, KICK_PERIOD_TONES, 0);

    /* If the DSP is already running, set turbo clock to reduce startup time.
    If the DSP is not running this call will fail. That is ignored since
    the DSP will subsequently be started when the first chain is created
    and it starts by default at turbo clock */
    appKymeraSetActiveDspClock(AUDIO_DSP_TURBO_CLOCK);

    PanicFalse(Kymera_OutputPrepare(output_user_prompt, &config));
}

static const chain_config_t * kymera_GetPromptChainConfig(promptFormat prompt_format, uint16 sample_rate)
{
    const chain_config_t *config = NULL;
    const bool requires_resampler = (KymeraOutput_GetAuxSampleRate() != sample_rate);

    if (prompt_format == PROMPT_FORMAT_SBC)
    {
        config = requires_resampler ? Kymera_GetChainConfigs()->chain_prompt_decoder_config : Kymera_GetChainConfigs()->chain_prompt_decoder_no_iir_config;
    }
    else if (prompt_format == PROMPT_FORMAT_PCM)
    {
        /* If PCM at the correct rate, no chain required at all. */
        config = requires_resampler ? Kymera_GetChainConfigs()->chain_prompt_pcm_config : NULL;
    }

    return config;
}

static const chain_config_t * kymera_GetToneChainConfig(uint16 sample_rate)
{
    const chain_config_t *config = NULL;
    const bool has_resampler = (KymeraOutput_GetAuxSampleRate() != sample_rate);

    config = has_resampler ? Kymera_GetChainConfigs()->chain_tone_gen_config : Kymera_GetChainConfigs()->chain_tone_gen_no_iir_config;

    return config;
}

static void kymera_CreateChain(const chain_config_t *config)
{
    PanicFalse(kymera_GetTonePromptChain() == NULL);

    kymeraTaskData *theKymera = KymeraGetTaskData();
    kymera_chain_handle_t chain = NULL;
    chain = ChainCreate(config);
    Operator op_resampler = ChainGetOperatorByRole(kymera_GetTonePromptChain(), OPR_TONE_PROMPT_RESAMPLER);
    unsigned aux_buffer_size = Kymera_OutputGetMainVolumeBufferSize();
    // The buffer size must be positive
    PanicZero(aux_buffer_size);

    if (op_resampler)
    {
        aux_buffer_size += PROMPT_TONE_RESAMPLER_FACTOR_MAX;
    }
    else
    {
        aux_buffer_size += PROMPT_TONE_OUTPUT_SIZE_SBC;
    }

    /* Configure pass-through buffer */
    Operator op = ChainGetOperatorByRole(chain, OPR_TONE_PROMPT_BUFFER);
    DEBUG_LOG("kymera_CreateTonePromptChain prompt buffer %u", op);
    OperatorsStandardSetBufferSize(op, aux_buffer_size);
    ChainConnect(chain);
    theKymera->chain_tone_handle = chain;
}

static void kymera_CreatePromptChain(promptFormat prompt_format, uint16 sample_rate)
{
    const chain_config_t *config = kymera_GetPromptChainConfig(prompt_format, sample_rate);

    /* NULL is a valid config for PCM prompt with no resampler */

    if (config)
    {
        kymera_CreateChain(config);
    }

    kymera_tone_state = kymera_tone_ready_prompt;
}

static void kymera_CreateToneChain(uint16 sample_rate)
{
    const chain_config_t *config = kymera_GetToneChainConfig(sample_rate);
    PanicNull((void *)config);
    kymera_CreateChain(config);
    kymera_tone_state = kymera_tone_ready_tone;
}

static void kymera_ConfigureTonePromptResampler(uint32 output_rate, uint32 tone_prompt_rate)
{
    Operator op = ChainGetOperatorByRole(kymera_GetTonePromptChain(), OPR_TONE_PROMPT_RESAMPLER);
    DEBUG_LOG("kymera_ConfigureTonePromptResampler resampler %u", op);
    OperatorsResamplerSetConversionRate(op, tone_prompt_rate, output_rate);
}

static Source kymera_ConfigurePromptChain(const KYMERA_INTERNAL_TONE_PROMPT_PLAY_T *msg)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();

    if (KymeraOutput_GetAuxSampleRate() != msg->rate)
    {
        kymera_ConfigureTonePromptResampler(KymeraOutput_GetAuxSampleRate(), msg->rate);
    }

    /* Configure prompt file source */
    theKymera->prompt_source = PanicNull(StreamFileSource(msg->prompt));
    DEBUG_LOG("kymera_ConfigurePromptChain prompt %u", msg->prompt);
    appKymeraPromptSourceSetup(theKymera->prompt_source);

    if (kymera_GetTonePromptChain())
    {
        PanicFalse(ChainConnectInput(kymera_GetTonePromptChain(), theKymera->prompt_source, EPR_PROMPT_IN));
    }
    else
    {
        /* No chain (prompt is PCM at the correct sample rate) so the source
        is just the file */
        return theKymera->prompt_source;
    }
    return ChainGetOutput(kymera_GetTonePromptChain(), EPR_TONE_PROMPT_CHAIN_OUT);
}

static Source kymera_ConfigureToneChain(const KYMERA_INTERNAL_TONE_PROMPT_PLAY_T *msg)
{
    Operator op;
    kymeraTaskData *theKymera = KymeraGetTaskData();

    if (KymeraOutput_GetAuxSampleRate() != msg->rate)
    {
         kymera_ConfigureTonePromptResampler(KymeraOutput_GetAuxSampleRate(), msg->rate);
    }

    /* Configure ringtone generator */
    op = ChainGetOperatorByRole(kymera_GetTonePromptChain(), OPR_TONE_GEN);
    DEBUG_LOG("kymera_ConfigureToneChain tone gen %u", op);
    OperatorsStandardSetSampleRate(op, msg->rate);
    OperatorsConfigureToneGenerator(op, msg->tone, &theKymera->task);

    return ChainGetOutput(kymera_GetTonePromptChain(), EPR_TONE_PROMPT_CHAIN_OUT);
}

bool appKymeraIsPlayingPrompt(void)
{
    return (kymera_tone_state == kymera_tone_playing);
}

static bool kymera_TonePromptIsReady(void)
{
    return ((kymera_tone_state == kymera_tone_ready_prompt) || (kymera_tone_state == kymera_tone_ready_tone));
}

static bool Kymera_TonePromptIsOnlyOutputChainUser(void)
{
    return ((appKymeraGetState() == KYMERA_STATE_TONE_PLAYING) || ((appKymeraGetState() == KYMERA_STATE_IDLE) && kymera_TonePromptIsReady()));
}

static bool kymera_CorrectPromptChainReady(promptFormat format)
{
    Operator sbc_decoder = ChainGetOperatorByRole(kymera_GetTonePromptChain(), OPR_PROMPT_DECODER);
    bool sbc_prompt_ready = ((format == PROMPT_FORMAT_SBC) && sbc_decoder);
    bool pcm_prompt_ready = ((format == PROMPT_FORMAT_PCM) && !sbc_decoder);

    return sbc_prompt_ready || pcm_prompt_ready;
}

static bool kymera_ResamplingNeedsMet(uint16 rate)
{
    Operator resampler = ChainGetOperatorByRole(kymera_GetTonePromptChain(), OPR_TONE_PROMPT_RESAMPLER);
    bool needs_resampler = (KymeraOutput_GetAuxSampleRate() != rate);
    bool resampling_needs_met = (resampler && needs_resampler) || (!resampler && !needs_resampler);

    return resampling_needs_met;
}

static bool kymera_CorrectTonePromptChainReady(const KYMERA_INTERNAL_TONE_PROMPT_PLAY_T *msg)
{
    bool tone_chain_ready_for_tone_play = ((kymera_tone_state == kymera_tone_ready_tone) && msg->tone != NULL);
    bool prompt_chain_ready_for_prompt_play = (((kymera_tone_state == kymera_tone_ready_prompt) && msg->prompt != FILE_NONE) &&
                                               kymera_CorrectPromptChainReady(msg->prompt_format));

    bool resampling_needs_met = kymera_ResamplingNeedsMet(msg->rate);

    DEBUG_LOG("kymera_CorrectTonePromptChainReady %u, tone ready %u, prompt ready %u, resampling needs met %u",
              ((tone_chain_ready_for_tone_play || prompt_chain_ready_for_prompt_play) && resampling_needs_met),
              tone_chain_ready_for_tone_play, prompt_chain_ready_for_prompt_play, resampling_needs_met);

    return((tone_chain_ready_for_tone_play || prompt_chain_ready_for_prompt_play) && resampling_needs_met);
}

static void kymera_SendStartInd(const KYMERA_INTERNAL_TONE_PROMPT_PLAY_T *msg)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();

    if(msg->tone)
    {
        KYMERA_NOTIFICATION_TONE_STARTED_T* message = PanicNull(malloc(sizeof(KYMERA_NOTIFICATION_TONE_STARTED_T)));
        message->tone = msg->tone;
        TaskList_MessageSend(theKymera->listeners, KYMERA_NOTIFICATION_TONE_STARTED, message);
    }
    else
    {
        KYMERA_NOTIFICATION_PROMPT_STARTED_T* message = PanicNull(malloc(sizeof(KYMERA_NOTIFICATION_PROMPT_STARTED_T)));
        message->id = msg->prompt;
        TaskList_MessageSend(theKymera->listeners, KYMERA_NOTIFICATION_PROMPT_STARTED, message);
    }
}

static Source kymera_PrepareInputChain(const KYMERA_INTERNAL_TONE_PROMPT_PLAY_T *msg)
{
    bool is_tone = (msg->tone != NULL);
    bool is_prompt = (msg->prompt != FILE_NONE);
    Source output = NULL;

    if(is_tone)
    {
        if(!kymera_TonePromptIsReady())
        {
            kymera_CreateToneChain(msg->rate);
        }
        output = kymera_ConfigureToneChain(msg);
    }
    else if(is_prompt)
    {
        if(!kymera_TonePromptIsReady())
        {
            kymera_CreatePromptChain(msg->prompt_format, msg->rate);
        }
        output = kymera_ConfigurePromptChain(msg);
    }

    return output;
}

void appKymeraHandleInternalTonePromptPlay(const KYMERA_INTERNAL_TONE_PROMPT_PLAY_T *msg)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();
    output_source_t output = {0};
    int16 volume_db = (msg->tone != NULL) ? KYMERA_CONFIG_TONE_VOLUME : KYMERA_CONFIG_PROMPT_VOLUME;

    DEBUG_LOG("appKymeraHandleInternalTonePromptPlay, prompt %x, tone %p, ttp %d, int %u, lock 0x%x, mask 0x%x",
                msg->prompt, msg->tone, msg->time_to_play, msg->interruptible, msg->client_lock, msg->client_lock_mask);

    kymera_SendStartInd(msg);

    /* If there is a tone still playing at this point, it must be an interruptable tone, so cut it off */
    if(appKymeraIsPlayingPrompt() || (!kymera_CorrectTonePromptChainReady(msg) && kymera_TonePromptIsReady()))
    {
        appKymeraTonePromptStop();
    }

    switch (appKymeraGetState())
    {
        case KYMERA_STATE_IDLE:
        case KYMERA_STATE_ADAPTIVE_ANC_STARTED:
            appKymeraSetState(KYMERA_STATE_TONE_PLAYING);
            break;
        default:
            break;
    }

    kymera_PrepareOutputChain(msg->rate);
    KymeraOutput_ChainStart();
    output.aux = kymera_PrepareInputChain(msg);
    PanicFalse(Kymera_OutputConnect(output_user_prompt, &output));
    KymeraOutput_SetAuxVolume(volume_db);

    if (KymeraOutput_SetAuxTtp(msg->time_to_play))
    {
        rtime_t now = SystemClockGetTimerTime();
        rtime_t delta = rtime_sub(msg->time_to_play, now);
        DEBUG_LOG("appKymeraHandleInternalTonePromptPlay now=%u, ttp=%u, left=%d", now, msg->time_to_play, delta);
    }

    /* Start tone */
    if (theKymera->chain_tone_handle)
    {
        ChainStart(theKymera->chain_tone_handle);
    }

    kymera_tone_state = kymera_tone_playing;
    /* May need to exit low power mode to play tone simultaneously */
    appKymeraConfigureDspPowerMode();

    if (!msg->interruptible)
    {
        appKymeraSetToneLock(theKymera);
    }
    theKymera->tone_client_lock = msg->client_lock;
    theKymera->tone_client_lock_mask = msg->client_lock_mask;
}

static void kymera_DestroyChain(void)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();

    Kymera_OutputDisconnect(output_user_prompt);

    if(theKymera->chain_tone_handle)
    {
        ChainDestroy(theKymera->chain_tone_handle);
        theKymera->chain_tone_handle = NULL;
    }

    if (Kymera_TonePromptIsOnlyOutputChainUser() && !Kymera_IsStandaloneLeakthroughActive())
    {
        /* Move back to idle state if standalone leak-through is not active */
        appKymeraSetState(KYMERA_STATE_IDLE);
		KymeraAdaptiveAnc_EnableAdaptivityPostTonePromptStop();
    }
}

void appKymeraTonePromptStop(void)
{
    kymeraTaskData *theKymera = KymeraGetTaskData();
   
    /* Exit if there isn't a tone or prompt playing */
    if ((!theKymera->chain_tone_handle) && (!theKymera->prompt_source) && (!kymera_TonePromptIsReady()))
    {
        return;
    }

    DEBUG_LOG("appKymeraTonePromptStop, state %u", appKymeraGetState());

    if(appKymeraIsPlayingPrompt())
    {
        KymeraOutput_SetAuxVolume(0);

        if (theKymera->prompt_source)
        {
            appKymeraPromptSourceClose(theKymera->prompt_source);
            theKymera->prompt_source = NULL;
        }

        if (theKymera->chain_tone_handle)
        {
            ChainStop(theKymera->chain_tone_handle);
        }
    }
            
    /* Keep framework enabled until after DSP clock update */
    OperatorsFrameworkEnable();

    kymera_DestroyChain();

    appKymeraClearToneLock(theKymera);

    if(appKymeraIsPlayingPrompt())
    {
        PanicZero(theKymera->tone_count);
        theKymera->tone_count--;
    }

    kymera_tone_state = kymera_tone_idle;

     /* Return to low power mode (if applicable) */
    appKymeraConfigureDspPowerMode();

    OperatorsFrameworkDisable();

    /* Tone now stopped, clear the client's lock */
    if (theKymera->tone_client_lock)
    {
        *theKymera->tone_client_lock &= ~theKymera->tone_client_lock_mask;
        theKymera->tone_client_lock = 0;
        theKymera->tone_client_lock_mask = 0;
    }
}

bool Kymera_PrepareForPrompt(promptFormat format, uint16 sample_rate)
{
    bool prepared = FALSE;

    if(kymera_tone_state == kymera_tone_idle)
    {
        kymeraTaskData *theKymera = KymeraGetTaskData();

        switch(appKymeraGetState())
        {
            case KYMERA_STATE_IDLE:
            case KYMERA_STATE_ADAPTIVE_ANC_STARTED:
            case KYMERA_STATE_SCO_ACTIVE:
            case KYMERA_STATE_SCO_ACTIVE_WITH_FORWARDING:
            case KYMERA_STATE_SCO_SLAVE_ACTIVE:
            case KYMERA_STATE_A2DP_STREAMING:
            case KYMERA_STATE_A2DP_STREAMING_WITH_FORWARDING:
            case KYMERA_STATE_STANDALONE_LEAKTHROUGH:
            case KYMERA_STATE_MIC_LOOPBACK:
            case KYMERA_STATE_WIRED_AUDIO_PLAYING:
            case KYMERA_STATE_LE_AUDIO_ACTIVE:
            case KYMERA_STATE_LE_VOICE_ACTIVE:
                kymera_PrepareOutputChain(sample_rate);
                kymera_CreatePromptChain(format, sample_rate);
                MessageSendLater(&theKymera->task, KYMERA_INTERNAL_PREPARE_FOR_PROMPT_TIMEOUT, NULL, PREPARE_FOR_PROMPT_TIMEOUT);
                prepared = TRUE;
                break;

            case KYMERA_STATE_TONE_PLAYING:
            default:
                UNUSED(theKymera);
                break;
        }
    }

    DEBUG_LOG("Kymera_PrepareForPrompt prepared %u, format %u rate %u", prepared, format, sample_rate);

    return prepared;
}

bool Kymera_IsReadyForPrompt(promptFormat format, uint16 sample_rate)
{
    bool resampling_needs_met = kymera_ResamplingNeedsMet(sample_rate);
    bool is_ready_for_prompt = ((kymera_tone_state == kymera_tone_ready_prompt) && kymera_CorrectPromptChainReady(format) && resampling_needs_met);

    DEBUG_LOG("Kymera_IsReadyForPrompt %u, format %u rate %u", is_ready_for_prompt, format, sample_rate);

    return is_ready_for_prompt;
}

void appKymeraTonePromptInit(void)
{
    Kymera_OutputRegister(&output_info);
}
