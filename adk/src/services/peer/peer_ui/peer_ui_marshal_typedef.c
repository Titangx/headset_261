/*!
    \copyright Copyright (c) 2022 Qualcomm Technologies International, Ltd.
        All Rights Reserved.
        Qualcomm Technologies International, Ltd. Confidential and Proprietary.
    \version %%version
    \file 
    \brief The peer_ui marshal type definitions. This file is generated by D:\Work\Qcc\qcc514x-qcc304x-src-1-0_qtil_standard_oem_headset-CS-r00261.1-b6022d9b12dafd39568892b0ca2f2d52672d950f\adk\tools/packages/typegen/typegen.py.
*/

#include <csrtypes.h>
#include <peer_ui.h>
#include <peer_signalling.h>
#include <ui_inputs.h>
#include <app/marshal/marshal_if.h>
#include <peer_ui_typedef.h>
#include <peer_ui_marshal_typedef.h>
#include <marshal_common.h>

#ifndef HOSTED_TEST_ENVIRONMENT
COMPILE_TIME_ASSERT(sizeof(ui_input_t) == sizeof(uint16), ui_input_t_is_not_the_same_size_as_uint16);
#endif

/*! Structure has ui_event and timestamp to send across to secondary */
static const marshal_member_descriptor_t peer_ui_event_t_member_descriptors[] =
{
    /*! UI Indication type */
    MAKE_MARSHAL_MEMBER(peer_ui_event_t, uint16, indication_type),
    /*! UI Indication index */
    MAKE_MARSHAL_MEMBER(peer_ui_event_t, uint16, indication_index),
    /*! Absolute time in microseconds when UI Event should be handled in secondary EB */
    MAKE_MARSHAL_MEMBER(peer_ui_event_t, marshal_rtime_t, timestamp),
} ;

/*! Structure has ui_input and timestamp to send across to secondary */
static const marshal_member_descriptor_t peer_ui_input_t_member_descriptors[] =
{
    /*! UI input sent from Primary EB to Secondary EB */
    MAKE_MARSHAL_MEMBER(peer_ui_input_t, ui_input_t, ui_input),
    /*! Absolute time in microseconds when UI Input should be handled in secondary EB */
    MAKE_MARSHAL_MEMBER(peer_ui_input_t, marshal_rtime_t, timestamp),
    /*! Data to be sent over to Secondary */
    MAKE_MARSHAL_MEMBER(peer_ui_input_t, uint8, data),
} ;

const marshal_type_descriptor_t marshal_type_descriptor_peer_ui_event_t = MAKE_MARSHAL_TYPE_DEFINITION(peer_ui_event_t, peer_ui_event_t_member_descriptors);
const marshal_type_descriptor_t marshal_type_descriptor_peer_ui_input_t = MAKE_MARSHAL_TYPE_DEFINITION(peer_ui_input_t, peer_ui_input_t_member_descriptors);


#define EXPAND_AS_TYPE_DEFINITION(type) (const marshal_type_descriptor_t *)&marshal_type_descriptor_##type,
const marshal_type_descriptor_t * const peer_ui_marshal_type_descriptors[] =
{
    MARSHAL_COMMON_TYPES_TABLE(EXPAND_AS_TYPE_DEFINITION)
    PEER_UI_MARSHAL_TYPES_TABLE(EXPAND_AS_TYPE_DEFINITION)
} ;

#undef EXPAND_AS_TYPE_DEFINITION

