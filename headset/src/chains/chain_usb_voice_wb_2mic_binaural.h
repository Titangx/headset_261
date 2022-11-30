/*!
    \copyright Copyright (c) 2022 Qualcomm Technologies International, Ltd.
        All Rights Reserved.
        Qualcomm Technologies International, Ltd. Confidential and Proprietary.
    \file chain_usb_voice_wb_2mic_binaural.h
    \brief The chain_usb_voice_wb_2mic_binaural chain.

    This file is generated by D:\Work\Qcc\qcc514x-qcc304x-src-1-0_qtil_standard_oem_headset-CS-r00261.1-b6022d9b12dafd39568892b0ca2f2d52672d950f\adk\tools\packages\chaingen\chaingen_mod\__init__.pyc.
*/

#ifndef _CHAIN_USB_VOICE_WB_2MIC_BINAURAL_H__
#define _CHAIN_USB_VOICE_WB_2MIC_BINAURAL_H__

/*!
\page chain_usb_voice_wb_2mic_binaural
@startuml
    object OPR_USB_AUDIO_TX
    OPR_USB_AUDIO_TX : id = CAP_ID_USB_AUDIO_TX
    object OPR_CVC_RECEIVE
    OPR_CVC_RECEIVE : id = CAP_ID_CVC_RECEIVE_WB
    object OPR_CVC_SEND
    OPR_CVC_SEND : id = CAP_ID_CVCHS2MIC_BINAURAL_SEND_WB
    OPR_USB_AUDIO_TX "IN(0)"<-- "OUT(0)" OPR_CVC_SEND
    object EPR_USB_CVC_RECEIVE_IN #lightgreen
    OPR_CVC_RECEIVE "IN(0)" <-- EPR_USB_CVC_RECEIVE_IN
    object EPR_CVC_SEND_REF_IN #lightgreen
    OPR_CVC_SEND "REFERENCE(0)" <-- EPR_CVC_SEND_REF_IN
    object EPR_CVC_SEND_IN1 #lightgreen
    OPR_CVC_SEND "IN1(1)" <-- EPR_CVC_SEND_IN1
    object EPR_CVC_SEND_IN2 #lightgreen
    OPR_CVC_SEND "IN2(2)" <-- EPR_CVC_SEND_IN2
    object EPR_SCO_SPEAKER #lightblue
    EPR_SCO_SPEAKER <-- "OUT(0)" OPR_CVC_RECEIVE
    object EPR_USB_TO_HOST #lightblue
    EPR_USB_TO_HOST <-- "USB_OUT(0)" OPR_USB_AUDIO_TX
@enduml
*/

#include <chain.h>

extern const chain_config_t chain_usb_voice_wb_2mic_binaural_config;

#endif /* _CHAIN_USB_VOICE_WB_2MIC_BINAURAL_H__ */
