/*!
    \copyright Copyright (c) 2022 Qualcomm Technologies International, Ltd.
        All Rights Reserved.
        Qualcomm Technologies International, Ltd. Confidential and Proprietary.
    \file chain_prompt_decoder.h
    \brief The chain_prompt_decoder chain.

    This file is generated by D:\Work\Qcc\qcc514x-qcc304x-src-1-0_qtil_standard_oem_headset-CS-r00261.1-b6022d9b12dafd39568892b0ca2f2d52672d950f\adk\tools\packages\chaingen\chaingen_mod\__init__.pyc.
*/

#ifndef _CHAIN_PROMPT_DECODER_H__
#define _CHAIN_PROMPT_DECODER_H__

/*!
\page chain_prompt_decoder
@startuml
    object OPR_PROMPT_DECODER
    OPR_PROMPT_DECODER : id = CAP_ID_SBC_DECODER
    object OPR_TONE_PROMPT_RESAMPLER
    OPR_TONE_PROMPT_RESAMPLER : id = CAP_ID_IIR_RESAMPLER
    object OPR_TONE_PROMPT_BUFFER
    OPR_TONE_PROMPT_BUFFER : id = CAP_ID_BASIC_PASS
    OPR_TONE_PROMPT_RESAMPLER "IN(0)"<-- "OUT(0)" OPR_PROMPT_DECODER
    OPR_TONE_PROMPT_BUFFER "IN(0)"<-- "OUT(0)" OPR_TONE_PROMPT_RESAMPLER
    object EPR_PROMPT_IN #lightgreen
    OPR_PROMPT_DECODER "IN(0)" <-- EPR_PROMPT_IN
    object EPR_TONE_PROMPT_CHAIN_OUT #lightblue
    EPR_TONE_PROMPT_CHAIN_OUT <-- "OUT(0)" OPR_TONE_PROMPT_BUFFER
@enduml
*/

#include <chain.h>

extern const chain_config_t chain_prompt_decoder_config_p0;

extern const chain_config_t chain_prompt_decoder_config_p1;

#endif /* _CHAIN_PROMPT_DECODER_H__ */

