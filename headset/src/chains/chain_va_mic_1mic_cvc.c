/*!
    \copyright Copyright (c) 2022 Qualcomm Technologies International, Ltd.
        All Rights Reserved.
        Qualcomm Technologies International, Ltd. Confidential and Proprietary.
    \file chain_va_mic_1mic_cvc.c
    \brief The chain_va_mic_1mic_cvc chain.

    This file is generated by D:\Work\Qcc\qcc514x-qcc304x-src-1-0_qtil_standard_oem_headset-CS-r00261.1-b6022d9b12dafd39568892b0ca2f2d52672d950f\adk\tools\packages\chaingen\chaingen_mod\__init__.pyc.
*/

#include <chain_va_mic_1mic_cvc.h>
#include <cap_id_prim.h>
#include <opmsg_prim.h>
#include <hydra_macros.h>
#include <../headset_chain_config.h>
#include <kymera_chain_roles.h>
static const operator_config_t operators[] =
{
    MAKE_OPERATOR_CONFIG(CAP_ID_BASIC_PASS, OPR_MIC_GAIN),
    MAKE_OPERATOR_CONFIG(CAP_ID_CVCHS1MIC_SEND_WB, OPR_CVC_SEND),
} ;

static const operator_endpoint_t inputs[] =
{
    {OPR_CVC_SEND, EPR_VA_MIC_AEC_IN, 0},
    {OPR_MIC_GAIN, EPR_VA_MIC_MIC1_IN, 0},
} ;

static const operator_endpoint_t outputs[] =
{
    {OPR_CVC_SEND, EPR_VA_MIC_ENCODE_OUT, CVC_1MIC_VA_OUTPUT_TERMINAL},
} ;

static const operator_connection_t connections[] =
{
    {OPR_MIC_GAIN, 0, OPR_CVC_SEND, 1, 1},
} ;

const chain_config_t chain_va_mic_1mic_cvc_config = {1, 0, operators, 2, inputs, 2, outputs, 1, connections, 1};

