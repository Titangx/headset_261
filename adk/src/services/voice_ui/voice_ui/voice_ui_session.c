/*!
\copyright  Copyright (c) 2021 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\file
\brief      Voice Assistant Session state
*/

#include "voice_ui_session.h"
#include "voice_ui.h"
#include "voice_ui_va_client_if.h"
#include "voice_ui_multipoint.h"
#include "av.h"
#include "logging.h"
#include <panic.h>

static bool voiceUi_PopulateSourceContext(audio_source_t source, audio_source_provider_context_t *context);
const av_context_provider_if_t provider_if =
{
    .PopulateSourceContext = voiceUi_PopulateSourceContext
};

static bool va_session_in_progress = FALSE;

static bool voiceUi_PopulateSourceContext(audio_source_t source, audio_source_provider_context_t *context)
{
    bool status = FALSE;

    if (va_session_in_progress)
    {
        status = VoiceUi_PopulateContextWithVaInProgress(source, context);
    }
    else
    {
        status = VoiceUi_PopulateContextWithVaIdle(source, context);
    }

    if (status)
    {
        DEBUG_LOG_DEBUG("voiceUi_PopulateSourceContext: enum:audio_source_t:%d set context as enum:audio_source_provider_context_t:%d", source, *context);
    }

    return status;
}

void VoiceUi_VaSessionStarted(voice_ui_handle_t* va_handle)
{
    if (VoiceUi_IsActiveAssistant(va_handle) && (va_session_in_progress != TRUE))
    {
        VoiceUi_PauseNonVaSource();
        va_session_in_progress = TRUE;
    }
}

void VoiceUi_VaSessionEnded(voice_ui_handle_t* va_handle)
{
    if (VoiceUi_IsActiveAssistant(va_handle))
    {
        VoiceUi_VaSessionReset();
    }
}

bool VoiceUi_IsSessionInProgress(void)
{
    return va_session_in_progress;
}

void VoiceUi_VaSessionReset(void)
{
    if (va_session_in_progress != FALSE)
    {
        VoiceUi_ResumeNonVaSource();
        va_session_in_progress = FALSE;
    }
}

void VoiceUi_VaSessionInit(void)
{
    PanicFalse(Av_RegisterContextProvider(&provider_if));
}
