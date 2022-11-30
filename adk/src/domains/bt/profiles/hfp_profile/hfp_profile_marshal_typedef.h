/*!
    \copyright Copyright (c) 2022 Qualcomm Technologies International, Ltd.
        All Rights Reserved.
        Qualcomm Technologies International, Ltd. Confidential and Proprietary.
    \version %%version
    \file 
    \brief The hfp_profile marshal type declarations. This file is generated by D:\Work\Qcc\qcc514x-qcc304x-src-1-0_qtil_standard_oem_headset-CS-r00261.1-b6022d9b12dafd39568892b0ca2f2d52672d950f\adk\tools/packages/typegen/typegen.py.
*/

#ifndef _HFP_PROFILE_MARSHAL_TYPEDEF_H__
#define _HFP_PROFILE_MARSHAL_TYPEDEF_H__

#include <csrtypes.h>
#include <marshal_common.h>
#include <hfp.h>
#include <task_list.h>
#include <battery_monitor.h>
#include <voice_sources_list.h>
#include <source_param_types.h>
#include <app/marshal/marshal_if.h>

extern const marshal_type_descriptor_t marshal_type_descriptor_hfpTaskDataBitfields;
extern const marshal_type_descriptor_t marshal_type_descriptor_hfp_link_prio_mapping_t;
extern const marshal_type_descriptor_t marshal_type_descriptor_hfpState;
extern const marshal_type_descriptor_t marshal_type_descriptor_hfpInstanceTaskData;

#define HFP_PROFILE_MARSHAL_TYPES_TABLE(ENTRY)\
    ENTRY(hfpTaskDataBitfields)\
    ENTRY(hfp_link_prio_mapping_t)\
    ENTRY(hfpState)\
    ENTRY(hfpInstanceTaskData)

#endif /* _HFP_PROFILE_MARSHAL_TYPEDEF_H__ */

