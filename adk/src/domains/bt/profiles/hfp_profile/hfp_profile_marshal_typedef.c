/*!
    \copyright Copyright (c) 2022 Qualcomm Technologies International, Ltd.
        All Rights Reserved.
        Qualcomm Technologies International, Ltd. Confidential and Proprietary.
    \version %%version
    \file 
    \brief The hfp_profile marshal type definitions. This file is generated by D:\Work\Qcc\qcc514x-qcc304x-src-1-0_qtil_standard_oem_headset-CS-r00261.1-b6022d9b12dafd39568892b0ca2f2d52672d950f\adk\tools/packages/typegen/typegen.py.
*/

#include <csrtypes.h>
#include <marshal_common.h>
#include <hfp.h>
#include <task_list.h>
#include <battery_monitor.h>
#include <voice_sources_list.h>
#include <source_param_types.h>
#include <app/marshal/marshal_if.h>
#include <hfp_profile_typedef.h>
#include <hfp_profile_marshal_typedef.h>

#include "domain_marshal_types.h"

#ifndef HOSTED_TEST_ENVIRONMENT
COMPILE_TIME_ASSERT(sizeof(hfp_wbs_codec_mask) == sizeof(uint16), hfp_wbs_codec_mask_is_not_the_same_size_as_uint16);
COMPILE_TIME_ASSERT(sizeof(source_state_t) == sizeof(uint8), source_state_t_is_not_the_same_size_as_uint8);
#endif

/*! HFP instance structure, contains all the information for an HFP connection */
static const marshal_member_descriptor_t hfpInstanceTaskData_member_descriptors[] =
{
    /*! Current state */
    MAKE_MARSHAL_MEMBER(hfpInstanceTaskData, hfpState, state),
    /*! HFP bitfields data */
    MAKE_MARSHAL_MEMBER(hfpInstanceTaskData, hfpTaskDataBitfields, bitfields),
    /*! Profile currently used */
    MAKE_MARSHAL_MEMBER(hfpInstanceTaskData, hfp_profile, profile),
    /*! Bitmap of supported SCO packets by both headset and AG */
    MAKE_MARSHAL_MEMBER(hfpInstanceTaskData, uint16, sco_supported_packets),
    /*! WB-Speech codec bit masks. */
    MAKE_MARSHAL_MEMBER(hfpInstanceTaskData, hfp_wbs_codec_mask, codec),
    /*! Wesco reported for the link. */
    MAKE_MARSHAL_MEMBER(hfpInstanceTaskData, uint8, wesco),
    /*! Qualcomm Codec Mode ID Selected - If in Qualcomm Codec Extensions mode. */
    MAKE_MARSHAL_MEMBER(hfpInstanceTaskData, uint16, qce_codec_mode_id),
    /*! Tesco reported for the link. */
    MAKE_MARSHAL_MEMBER(hfpInstanceTaskData, uint8, tesco),
    /*! Routing state of the source. */
    MAKE_MARSHAL_MEMBER(hfpInstanceTaskData, source_state_t, source_state),
} ;

const marshal_type_descriptor_t marshal_type_descriptor_hfpTaskDataBitfields = MAKE_MARSHAL_TYPE_DEFINITION_BASIC(hfpTaskDataBitfields);
const marshal_type_descriptor_t marshal_type_descriptor_hfp_link_prio_mapping_t = MAKE_MARSHAL_TYPE_DEFINITION_BASIC(hfp_link_prio_mapping_t);
const marshal_type_descriptor_t marshal_type_descriptor_hfpState = MAKE_MARSHAL_TYPE_DEFINITION_BASIC(hfpState);
const marshal_type_descriptor_t marshal_type_descriptor_hfpInstanceTaskData = MAKE_MARSHAL_TYPE_DEFINITION(hfpInstanceTaskData, hfpInstanceTaskData_member_descriptors);


