/*!
\copyright  Copyright (c) 2021 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\file
\brief      Voice UI Multipoint
*/

#include "voice_ui_multipoint.h"
#include "voice_ui_container.h"
#include "av.h"
#include <logging.h>
#include <message.h>

#define CLEAR_ROUTED_SOURCE_TIMEOUT (1500)

typedef enum
{
    CLEAR_ROUTED_SOURCE
} internal_msg_ids_t;

static void voiceUi_MessageHandler(Task, MessageId, Message);
static const TaskData msg_handler = {voiceUi_MessageHandler};

static audio_source_t previously_routed_source = audio_source_none;

static const bdaddr * voiceUi_GetVaSourceBdAddress(void)
{
    const voice_ui_handle_t * va = VoiceUi_GetActiveVa();

    if (va)
    {
        return va->voice_assistant->GetBtAddress();
    }

    return NULL;
}

static const bdaddr * voiceUi_GetA2dpSourceBdAddress(audio_source_t source)
{
    const avInstanceTaskData *av = Av_GetInstanceForHandsetSource(source);

    if (av)
    {
        return &(av->bd_addr);
    }

    return NULL;
}

static bool voiceUi_IsValidBtAddress(const bdaddr *addr)
{
    return addr && !BdaddrIsZero(addr);
}

static bool voiceUi_IsSameBtAdress(const bdaddr *addr_1, const bdaddr *addr_2)
{
    if (voiceUi_IsValidBtAddress(addr_1) && voiceUi_IsValidBtAddress(addr_2))
    {
        return BdaddrIsSame(addr_1, addr_2);
    }

    return FALSE;
}

static audio_source_t voiceUi_GetVaAudioSource(void)
{
    const bdaddr *va_addr = voiceUi_GetVaSourceBdAddress();
    audio_source_t source;
    for_all_a2dp_audio_sources(source)
    {
        if (voiceUi_IsSameBtAdress(va_addr, voiceUi_GetA2dpSourceBdAddress(source)))
        {
            return source;
        }
    }

    return audio_source_none;
}

static void voiceUi_CancelMessage(MessageId id)
{
    MessageCancelAll((Task) &msg_handler, id);
}

static void voiceUi_SendMessage(MessageId id, uint32 delay_in_ms)
{
    if (delay_in_ms)
    {
        MessageSendLater((Task) &msg_handler, id, NULL, delay_in_ms);
    }
    else
    {
        MessageSend((Task) &msg_handler, id, NULL);
    }
}

static void voiceUi_MessageHandler(Task task, MessageId id, Message msg)
{
    UNUSED(task);
    UNUSED(msg);
    if (id == CLEAR_ROUTED_SOURCE)
    {
        DEBUG_LOG_INFO("voiceUi_MessageHandler: CLEAR_ROUTED_SOURCE enum:audio_source_t:%d", previously_routed_source);
        previously_routed_source = audio_source_none;
    }
}

void VoiceUi_PauseNonVaSource(void)
{
    audio_source_t routed_source = AudioSources_GetRoutedSource();
    voiceUi_CancelMessage(CLEAR_ROUTED_SOURCE);
    if ((routed_source != audio_source_none) && (routed_source != voiceUi_GetVaAudioSource()))
    {
        DEBUG_LOG_DEBUG("VoiceUi_PauseNonVaSource: routed_source enum:audio_source_t:%d", routed_source);
        AudioSources_PauseAll();
        previously_routed_source = routed_source;
    }
}

void VoiceUi_ResumeNonVaSource(void)
{
    voiceUi_CancelMessage(CLEAR_ROUTED_SOURCE);
    if ((previously_routed_source != audio_source_none) && (previously_routed_source != voiceUi_GetVaAudioSource()))
    {
        DEBUG_LOG_DEBUG("VoiceUi_ResumeNonVaSource: enum:audio_source_t:%d", previously_routed_source);
        AudioSources_Play(previously_routed_source);
        voiceUi_SendMessage(CLEAR_ROUTED_SOURCE, CLEAR_ROUTED_SOURCE_TIMEOUT);
    }
}

bool VoiceUi_PopulateContextWithVaInProgress(audio_source_t source, audio_source_provider_context_t *context)
{
    UNUSED(source);
    UNUSED(context);
    if (source == voiceUi_GetVaAudioSource())
    {
        *context = context_audio_is_va_response;
        return TRUE;
    }

    return FALSE;
}

bool VoiceUi_PopulateContextWithVaIdle(audio_source_t source, audio_source_provider_context_t *context)
{
    UNUSED(source);
    UNUSED(context);
    if ((previously_routed_source == source) && (previously_routed_source != voiceUi_GetVaAudioSource()))
    {
        *context = context_audio_is_resumed;
        return TRUE;
    }

    return FALSE;
}
