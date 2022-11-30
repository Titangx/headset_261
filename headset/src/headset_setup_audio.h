/*!
\copyright  Copyright (c) 2019 - 2020 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version    %%version
\file       headset_setup_audio.h
\brief      Module Conifgure Audio chains for headset application
*/
#ifndef HEADESET_SETUP_AUDIO_H
#define HEADESET_SETUP_AUDIO_H

/*! \brief Configure and Set the Audio chains for Headset.
    \param void.
    \return TRUE if audio configuration is success.
*/

bool headsetSetupAudio(void);

/*! \brief Configures downloadable capabilities.
    \param void.
    \return void
*/
void headsetSetBundlesConfig(void);

#endif /* HEADESET_SETUP_AUDIO_H */
