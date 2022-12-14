/*!
\copyright  Copyright (c) 2020 Qualcomm Technologies International, Ltd.\n
            All Rights Reserved.\n
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\file
\brief      Configuration of application chains
*/

#ifndef HEADSET_CHAIN_CONFIG_H_
#define HEADSET_CHAIN_CONFIG_H_

#include "headset_cap_ids.h"

#if EB_CAP_ID_CVC_VA_1MIC == EB_CAP_ID_CVC_FBC
#define CVC_1MIC_VA_OUTPUT_TERMINAL 0
#else
#define CVC_1MIC_VA_OUTPUT_TERMINAL 1
#endif

#endif // HEADSET_CHAIN_CONFIG_H_
