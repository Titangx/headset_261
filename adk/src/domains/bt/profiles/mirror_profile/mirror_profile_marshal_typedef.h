/*!
    \copyright Copyright (c) 2022 Qualcomm Technologies International, Ltd.
        All Rights Reserved.
        Qualcomm Technologies International, Ltd. Confidential and Proprietary.
    \version %%version
    \file 
    \brief The mirror_profile marshal type declarations. This file is generated by D:\Work\Qcc\qcc514x-qcc304x-src-1-0_qtil_standard_oem_headset-CS-r00261.1-b6022d9b12dafd39568892b0ca2f2d52672d950f\adk\tools/packages/typegen/typegen.py.
*/

#ifndef _MIRROR_PROFILE_MARSHAL_TYPEDEF_H__
#define _MIRROR_PROFILE_MARSHAL_TYPEDEF_H__

#include <csrtypes.h>
#include <kymera_adaptation_voice_protected.h>
#include <app/marshal/marshal_if.h>
#include <marshal_common.h>


#define MIRROR_PROFILE_MARSHAL_TYPES_TABLE(ENTRY)\
    ENTRY(mirror_profile_hfp_volume_ind_t)\
    ENTRY(mirror_profile_hfp_codec_and_volume_ind_t)\
    ENTRY(mirror_profile_a2dp_volume_ind_t)\
    ENTRY(mirror_profile_stream_context_t)\
    ENTRY(mirror_profile_stream_context_response_t)\
    ENTRY(mirror_profile_sync_unmute_ind_t)

#define EXPAND_AS_ENUMERATION(type) MARSHAL_TYPE(type),
enum MIRROR_PROFILE_MARSHAL_TYPES
{
    DUMMY = (NUMBER_OF_COMMON_MARSHAL_OBJECT_TYPES-1),
    MIRROR_PROFILE_MARSHAL_TYPES_TABLE(EXPAND_AS_ENUMERATION)
    NUMBER_OF_MIRROR_PROFILE_MARSHAL_TYPES
} ;

#undef EXPAND_AS_ENUMERATION

extern const marshal_type_descriptor_t * const mirror_profile_marshal_type_descriptors[];
#endif /* _MIRROR_PROFILE_MARSHAL_TYPEDEF_H__ */

