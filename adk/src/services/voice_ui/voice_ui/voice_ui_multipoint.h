/*!
\copyright  Copyright (c) 2021 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\file
\brief      Private APIs for Voice UI Multipoint
*/

#ifndef VOICE_UI_MULTIPOINT_H_
#define VOICE_UI_MULTIPOINT_H_

#include "audio_sources.h"

/*! \brief Pause non-VA sources
*/
void VoiceUi_PauseNonVaSource(void);

/*! \brief Resume the source previously paused
*/
void VoiceUi_ResumeNonVaSource(void);

/*! \brief Populate source context when VA session is in progress
    \return TRUE if populated, FALSE otherwise
*/
bool VoiceUi_PopulateContextWithVaInProgress(audio_source_t source, audio_source_provider_context_t *context);

/*! \brief Populate source context when VA is idle (no session in progress)
    \return TRUE if populated, FALSE otherwise
*/
bool VoiceUi_PopulateContextWithVaIdle(audio_source_t source, audio_source_provider_context_t *context);

#endif // VOICE_UI_MULTIPOINT_H_
