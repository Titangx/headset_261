/*!
    \copyright Copyright (c) 2022 Qualcomm Technologies International, Ltd.
        All Rights Reserved.
        Qualcomm Technologies International, Ltd. Confidential and Proprietary.
    \version %%version
    \file 
    \brief The a2dp marshal type declarations. This file is generated by D:\Work\Qcc\qcc514x-qcc304x-src-1-0_qtil_standard_oem_headset-CS-r00261.1-b6022d9b12dafd39568892b0ca2f2d52672d950f\adk\tools/packages/typegen/typegen.py.
*/

#ifndef _A2DP_MARSHAL_TYPEDEF_H__
#define _A2DP_MARSHAL_TYPEDEF_H__

#include <csrtypes.h>
#include <marshal_common.h>
#include <audio_sync.h>
#include <audio_sources_audio_interface.h>
#include <app/marshal/marshal_if.h>

extern const marshal_type_descriptor_t marshal_type_descriptor_a2dpTaskDataBitfields;
extern const marshal_type_descriptor_t marshal_type_descriptor_avA2dpState;
extern const marshal_type_descriptor_t marshal_type_descriptor_avSuspendReason;
extern const marshal_type_descriptor_t marshal_type_descriptor_a2dpTaskData;

#define A2DP_MARSHAL_TYPES_TABLE(ENTRY)\
    ENTRY(a2dpTaskDataBitfields)\
    ENTRY(avA2dpState)\
    ENTRY(avSuspendReason)\
    ENTRY(a2dpTaskData)

#endif /* _A2DP_MARSHAL_TYPEDEF_H__ */

