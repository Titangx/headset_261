/*!
    \copyright Copyright (c) 2022 Qualcomm Technologies International, Ltd.
        All Rights Reserved.
        Qualcomm Technologies International, Ltd. Confidential and Proprietary.
    \file chain_va_graph_manager.c
    \brief The chain_va_graph_manager chain.

    This file is generated by D:\Work\Qcc\qcc514x-qcc304x-src-1-0_qtil_standard_oem_headset-CS-r00261.1-b6022d9b12dafd39568892b0ca2f2d52672d950f\adk\tools\packages\chaingen\chaingen_mod\__init__.pyc.
*/

#include <chain_va_graph_manager.h>
#include <cap_id_prim.h>
#include <opmsg_prim.h>
#include <hydra_macros.h>
#include <../headset_cap_ids.h>
#include <kymera_chain_roles.h>
static const operator_config_t operators[] =
{
    MAKE_OPERATOR_CONFIG(HS_CAP_ID_VA_GRAPH_MANAGER, OPR_VA_GRAPH_MANAGER),
} ;

const chain_config_t chain_va_graph_manager_config = {0, 0, operators, 1, NULL, 0, NULL, 0, NULL, 0};

