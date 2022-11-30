/*!
    \copyright Copyright (c) 2022 Qualcomm Technologies International, Ltd.
        All Rights Reserved.
        Qualcomm Technologies International, Ltd. Confidential and Proprietary.
    \version %%version
    \file 
    \brief The dfu_peer_sig c type definitions. This file is generated by D:\Work\Qcc\qcc514x-qcc304x-src-1-0_qtil_standard_oem_headset-CS-r00261.1-b6022d9b12dafd39568892b0ca2f2d52672d950f\adk\tools/packages/typegen/typegen.py.
*/

#ifndef _DFU_PEER_SIG_TYPEDEF_H__
#define _DFU_PEER_SIG_TYPEDEF_H__

#include <csrtypes.h>

/*! Peer Erase Request/Response exchanged between Primary and Secondary. */
typedef struct 
{
    /*! Peer Erase Request/Response; TRUE = Req Pri->Sec Start Erase, FALSE = Res Sec->Pri Indicate Erase Completed */
    uint8 peer_erase_req_res;
    /*! Peer Erase Status; DontCare for Req Pri->Sec, Valid for Res Sec->Pri, TRUE Res Sec->Pri Erase Succeeded else Failed */
    uint8 peer_erase_status;
} dfu_peer_erase_req_res_t;

/*! Peer indication about device not in use from Primary to Secondary. */
typedef struct 
{
    /*! unused */
    uint8 dummy;
} dfu_peer_device_not_in_use_t;

#endif /* _DFU_PEER_SIG_TYPEDEF_H__ */

