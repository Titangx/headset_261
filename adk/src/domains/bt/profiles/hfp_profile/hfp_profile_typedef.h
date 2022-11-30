/*!
    \copyright Copyright (c) 2022 Qualcomm Technologies International, Ltd.
        All Rights Reserved.
        Qualcomm Technologies International, Ltd. Confidential and Proprietary.
    \version %%version
    \file 
    \brief The hfp_profile c type definitions. This file is generated by D:\Work\Qcc\qcc514x-qcc304x-src-1-0_qtil_standard_oem_headset-CS-r00261.1-b6022d9b12dafd39568892b0ca2f2d52672d950f\adk\tools/packages/typegen/typegen.py.
*/

#ifndef _HFP_PROFILE_TYPEDEF_H__
#define _HFP_PROFILE_TYPEDEF_H__

#include <csrtypes.h>
#include <marshal_common.h>
#include <hfp.h>
#include <task_list.h>
#include <battery_monitor.h>
#include <voice_sources_list.h>
#include <source_param_types.h>

/*! Defines  */
#define HFP_SLC_STATUS_NOTIFY_LIST_INIT_CAPACITY 1
#define HFP_STATUS_NOTIFY_LIST_INIT_CAPACITY 6

#define MARSHAL_TYPE_hfp_wbs_codec_mask MARSHAL_TYPE_uint16
#define MARSHAL_TYPE_source_state_t MARSHAL_TYPE_uint8

/*! HFP task bitfields data structure */
typedef struct 
{
    /*! AG supports in-band ringing tone */
    unsigned in_band_ring:1;
    /*! Voice recognition is active */
    unsigned voice_recognition_active:1;
    /*! Voice recognition pending */
    unsigned voice_recognition_request:1;
    /*! Microphone mute is active */
    unsigned mute_active:1;
    /*! Caller ID is active */
    unsigned caller_id_active:1;
    /*! Flags indicating if we should connect/disconnect silently */
    unsigned flags:3;
    /*! Current call state */
    hfp_call_state call_state:4;
    /*! Flag indicating if we have accepted the call */
    unsigned call_accepted:1;
    /*! Flag indicating if we connect AV after SCO */
    unsigned sco_av_reconnect:1;
    /*! Flag indicating if we need UI indication for SCO */
    unsigned sco_ui_indication:1;
    /*! Flag indicating if ACL is encrypted */
    unsigned encrypted:1;
    /*! Flag indicating that ACL detach is pending */
    unsigned detach_pending:1;
    /*! Flag indicating the disconnect reason */
    unsigned disconnect_reason:4;
    /*! HF indicator assigned number */
    unsigned hf_indicator_assigned_num:2;
    /*! eSCO is connecting */
    unsigned esco_connecting:1;
} hfpTaskDataBitfields;

/*! HFP link priority to Voice Source mapping structure */
typedef struct 
{
    /*! Mapping table to get the Voice Source currently assigned to a given HFP library link priority */
    voice_source_t link_prio_to_voice_source_map[3];
    /*! Mapping table to get the HFP library link priority associated with a given Voice Source */
    hfp_link_priority voice_source_to_link_prio_map[max_voice_sources];
} hfp_link_prio_mapping_t;

/*! Application HFP state machine states */
typedef enum 
{
    /*! Initial state */
    HFP_STATE_NULL,
    /*! Initialising HFP instance */
    HFP_STATE_INITIALISING_HFP,
    /*! No HFP connection */
    HFP_STATE_DISCONNECTED,
    /*! Locally initiated connection in progress */
    HFP_STATE_CONNECTING_LOCAL,
    /*! Remotely initiated connection is progress */
    HFP_STATE_CONNECTING_REMOTE,
    /* HFP_STATE_CONNECTD (Parent state) */
    /*! HFP connected, no call in progress */
    HFP_STATE_CONNECTED_IDLE,
    /*! HFP connected, outgoing call in progress */
    HFP_STATE_CONNECTED_OUTGOING,
    /*! HFP connected, incoming call in progress */
    HFP_STATE_CONNECTED_INCOMING,
    /*! HFP connected, active call in progress */
    HFP_STATE_CONNECTED_ACTIVE,
    /*! HFP disconnecting in progress */
    HFP_STATE_DISCONNECTING
} hfpState;

/*! HFP instance structure, contains all the information for an HFP connection */
typedef struct 
{
    /*! HFP task */
    TaskData task;
    /*! Current state */
    hfpState state;
    /*! HFP operation lock, used to serialise HFP operations */
    uint16 hfp_lock;
    /*! HFP bitfields data */
    hfpTaskDataBitfields bitfields;
    /*! Profile currently used */
    hfp_profile profile;
    /*! Bitmap of supported SCO packets by both headset and AG */
    uint16 sco_supported_packets;
    /*! Address of connected AG */
    bdaddr ag_bd_addr;
    /*! Sink for SCO, 0 if no SCO active */
    Sink sco_sink;
    /*! Sink for SLC */
    Sink slc_sink;
    /*! WB-Speech codec bit masks. */
    hfp_wbs_codec_mask codec;
    /*! Wesco reported for the link. */
    uint8 wesco;
    /*! Qualcomm Codec Mode ID Selected - If in Qualcomm Codec Extensions mode. */
    uint16 qce_codec_mode_id;
    /*! Tesco reported for the link. */
    uint8 tesco;
    /*! Routing state of the source. */
    source_state_t source_state;
} hfpInstanceTaskData;

#endif /* _HFP_PROFILE_TYPEDEF_H__ */
