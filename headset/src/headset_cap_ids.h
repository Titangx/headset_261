/*!
\copyright  Copyright (c) 2017 - 2020 Qualcomm Technologies International, Ltd.\n
            All Rights Reserved.\n
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\file
\brief      Definitions of audio capability IDs
*/

#ifndef HEADSET_CAP_IDS_H
#define HEADSET_CAP_IDS_H

#ifdef INCLUDE_GAA
#include <gaa_cap_ids.h>
#endif

#ifdef INCLUDE_AMA
#include <ama_cap_ids.h>
#endif

#if (defined(__QCC304X__) || defined(__QCC514X__)) && !defined(HAVE_STP_ROM_1_2)
#define DOWNLOAD_APTX_ADAPTIVE_DECODE
#elif !defined(HAVE_STR_ROM_2_0_1) && !defined(HAVE_STP_ROM_1_2)
#define DOWNLOAD_AEC_REF
#endif

#if (defined(__QCC304X__) || defined(__QCC514X__) || defined(__QCC302X_APPS__) || defined(__QCC512X_APPS__)) && !defined(HAVE_STP_ROM_1_2)
#define DOWNLOAD_CVC_FBC
#endif


#ifdef DOWNLOAD_AEC_REF
#define HS_CAP_ID_AEC_REF                CAP_ID_DOWNLOAD_AEC_REFERENCE
#else
#define HS_CAP_ID_AEC_REF                CAP_ID_AEC_REFERENCE
#endif

#ifdef DOWNLOAD_VOLUME_CONTROL
#define HS_CAP_ID_VOL_CTRL_VOL           CAP_ID_DOWNLOAD_VOL_CTRL_VOL
#else
#define HS_CAP_ID_VOL_CTRL_VOL           CAP_ID_VOL_CTRL_VOL
#endif

#ifdef DOWNLOAD_APTX_ADAPTIVE_DECODE
#define HS_CAP_ID_APTX_ADAPTIVE_DECODE   CAP_ID_DOWNLOAD_APTX_ADAPTIVE_DECODE
#else
#define HS_CAP_ID_APTX_ADAPTIVE_DECODE   CAP_ID_APTX_ADAPTIVE_DECODE
#endif

#ifdef DOWNLOAD_SWITCHED_PASSTHROUGH
#define HS_CAP_ID_SWITCHED_PASSTHROUGH   CAP_ID_DOWNLOAD_SWITCHED_PASSTHROUGH_CONSUMER
#else
#define HS_CAP_ID_SWITCHED_PASSTHROUGH   CAP_ID_SWITCHED_PASSTHROUGH_CONSUMER
#endif

#ifdef DOWNLOAD_AAC_DECODER
#define HS_CAP_ID_AAC_DECODER            CAP_ID_DOWNLOAD_AAC_DECODER
#else
#define HS_CAP_ID_AAC_DECODER            CAP_ID_AAC_DECODER
#endif

#ifdef DOWNLOAD_VA_GRAPH_MANAGER
#define HS_CAP_ID_VA_GRAPH_MANAGER CAP_ID_DOWNLOAD_VA_GRAPH_MANAGER
#else
#define HS_CAP_ID_VA_GRAPH_MANAGER CAP_ID_VA_GRAPH_MANAGER
#endif

#ifdef DOWNLOAD_CVC_FBC
#define HS_CAP_ID_CVC_FBC CAP_ID_DOWNLOAD_CVC_FBC
#else
#define HS_CAP_ID_CVC_FBC CAP_ID_CVC_FBC
#endif

#define HS_CAP_ID_CVC_VA_1MIC HS_CAP_ID_CVC_FBC


#if defined(__QCC304X__) || defined(__QCC514X__)
#define HS_CAP_ID_CVC_VA_2MIC CAP_ID_CVCHS2MIC_BARGEIN_WB
#else
#define HS_CAP_ID_CVC_VA_2MIC CAP_ID_CVCHS2MIC_MONO_SEND_WB
#endif

#ifdef DOWNLOAD_GVA
#define HS_CAP_ID_GVA CAP_ID_DOWNLOAD_GVA
#else
    #if defined(INCLUDE_GAA) && defined(INCLUDE_WUW)
        #error GAA with Wake-up word functionality needs the DOWNLOAD_GVA define and download_gva capability
    #endif
#define HS_CAP_ID_GVA CAP_ID_NONE
#endif

#ifdef DOWNLOAD_APVA
#define HS_CAP_ID_APVA CAP_ID_DOWNLOAD_APVA
#else
    #if defined(INCLUDE_AMA) && defined(INCLUDE_WUW)
        #error AMA with Wake-up word functionality needs the DOWNLOAD_APVA define and download_apva capability
    #endif
#define HS_CAP_ID_APVA CAP_ID_NONE
#endif

#ifdef DOWNLOAD_SWBS
    #define HS_CAP_ID_SWBS_ENC CAP_ID_DOWNLOAD_SWBS_ENC
    #define HS_CAP_ID_SWBS_DEC CAP_ID_DOWNLOAD_SWBS_DEC
#else
        #define HS_CAP_ID_SWBS_ENC CAP_ID_SWBS_ENC
        #define HS_CAP_ID_SWBS_DEC CAP_ID_SWBS_DEC
#endif

#endif // HEADSET_CAP_IDS_H
