/*!
    \copyright Copyright (c) 2022 Qualcomm Technologies International, Ltd.
        All Rights Reserved.
        Qualcomm Technologies International, Ltd. Confidential and Proprietary.
    \file chain_usb_voice_nb_2mic_binaural.c
    \brief The chain_usb_voice_nb_2mic_binaural chain.

    This file is generated by D:\Work\Qcc\qcc514x-qcc304x-src-1-0_qtil_standard_oem_headset-CS-r00261.1-b6022d9b12dafd39568892b0ca2f2d52672d950f\adk\tools\packages\chaingen\chaingen_mod\__init__.pyc.
*/

#include <chain_usb_voice_nb_2mic_binaural.h>
#include <cap_id_prim.h>
#include <opmsg_prim.h>
#include <hydra_macros.h>
#include <../headset_cap_ids.h>
#include <kymera_chain_roles.h>
static const operator_config_t operators[] =
{
    MAKE_OPERATOR_CONFIG_PRIORITY_HIGH(CAP_ID_USB_AUDIO_TX, OPR_USB_AUDIO_TX),
    MAKE_OPERATOR_CONFIG(CAP_ID_CVC_RECEIVE_NB, OPR_CVC_RECEIVE),
    MAKE_OPERATOR_CONFIG(CAP_ID_CVCHS2MIC_BINAURAL_SEND_NB, OPR_CVC_SEND),
} ;

static const operator_endpoint_t inputs[] =
{
    {OPR_CVC_RECEIVE, EPR_USB_CVC_RECEIVE_IN, 0},
    {OPR_CVC_SEND, EPR_CVC_SEND_REF_IN, 0},
    {OPR_CVC_SEND, EPR_CVC_SEND_IN1, 1},
    {OPR_CVC_SEND, EPR_CVC_SEND_IN2, 2},
} ;

static const operator_endpoint_t outputs[] =
{
    {OPR_CVC_RECEIVE, EPR_SCO_SPEAKER, 0},
    {OPR_USB_AUDIO_TX, EPR_USB_TO_HOST, 0},
} ;

static const operator_connection_t connections[] =
{
    {OPR_CVC_SEND, 0, OPR_USB_AUDIO_TX, 0, 1},
} ;

const chain_config_t chain_usb_voice_nb_2mic_binaural_config = {0, 0, operators, 3, inputs, 4, outputs, 2, connections, 1};

