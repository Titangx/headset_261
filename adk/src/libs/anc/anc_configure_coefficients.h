/*******************************************************************************
Copyright (c) 2017-2020 Qualcomm Technologies International, Ltd.


FILE NAME
    anc_configure_coefficients.h

DESCRIPTION

*/

#ifndef ANC_CONFIGURE_COEFFICIENTS_H_
#define ANC_CONFIGURE_COEFFICIENTS_H_

#include <app/audio/audio_if.h>
#include "anc_config_data.h"
#include "anc_data.h"

anc_instance_config_t * getInstanceConfig(audio_anc_instance instance);
void ancConfigureMutePathGains(void);
void ancConfigureFilterCoefficients(void);
void ancConfigureFilterPathGains(void);
bool ancConfigureGainForFFApath(audio_anc_instance instance, uint8 gain);
bool ancConfigureGainForFFBpath(audio_anc_instance instance, uint8 gain);
bool ancConfigureGainForFBpath(audio_anc_instance instance, uint8 gain);
bool ancConfigureParallelGainForFFApath(uint8 gain_instance_zero, uint8 gain_instance_one);
bool ancConfigureParallelGainForFFBpath(uint8 gain_instance_zero, uint8 gain_instance_one);
bool ancConfigureParallelGainForFBpath(uint8 gain_instance_zero, uint8 gain_instance_one);
void ancOverWriteWithUserPathGains(void);
void ancConfigureParallelFilterMutePathGains(void);
void ancConfigureParallelFilterCoefficients(void);
void ancConfigureParallelFilterPathGains(void);
void ancEnableOutMix(void);
uint32 getAncInstanceMask(anc_instance_mask_t instance_mask);

#endif /* ANC_CONFIGURE_COEFFICIENTS_H_ */
