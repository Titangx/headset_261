/*!
    \copyright Copyright (c) 2022 Qualcomm Technologies International, Ltd.
        All Rights Reserved.
        Qualcomm Technologies International, Ltd. Confidential and Proprietary.
    \version %%version
    \file 
    \brief The connection_manager_list marshal type declarations. This file is generated by D:\Work\Qcc\qcc514x-qcc304x-src-1-0_qtil_standard_oem_headset-CS-r00261.1-b6022d9b12dafd39568892b0ca2f2d52672d950f\adk\tools/packages/typegen/typegen.py.
*/

#ifndef _CONNECTION_MANAGER_LIST_MARSHAL_TYPEDEF_H__
#define _CONNECTION_MANAGER_LIST_MARSHAL_TYPEDEF_H__

#include <csrtypes.h>
#include <marshal_common.h>
#include <task_list.h>
#include <connection_manager.h>
#include <link_policy.h>
#include <connection_no_ble.h>
#include <app/marshal/marshal_if.h>

extern const marshal_type_descriptor_t marshal_type_descriptor_cm_connection_state_t;
extern const marshal_type_descriptor_t marshal_type_descriptor_cm_con_bitfields_t;
extern const marshal_type_descriptor_t marshal_type_descriptor_cm_connection_t;

#define CONNECTION_MANAGER_LIST_MARSHAL_TYPES_TABLE(ENTRY)\
    ENTRY(cm_connection_state_t)\
    ENTRY(cm_con_bitfields_t)\
    ENTRY(cm_connection_t)

#endif /* _CONNECTION_MANAGER_LIST_MARSHAL_TYPEDEF_H__ */

