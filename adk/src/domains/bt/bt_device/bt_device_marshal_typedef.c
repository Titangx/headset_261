/*!
    \copyright Copyright (c) 2022 Qualcomm Technologies International, Ltd.
        All Rights Reserved.
        Qualcomm Technologies International, Ltd. Confidential and Proprietary.
    \version %%version
    \file 
    \brief The bt_device marshal type definitions. This file is generated by D:\Work\Qcc\qcc514x-qcc304x-src-1-0_qtil_standard_oem_headset-CS-r00261.1-b6022d9b12dafd39568892b0ca2f2d52672d950f\adk\tools/packages/typegen/typegen.py.
*/

#include <csrtypes.h>
#include <csrtypes.h>
#include <marshal_common.h>
#include <app/marshal/marshal_if.h>
#include <bt_device_typedef.h>
#include <bt_device_marshal_typedef.h>



/*! Device attributes store in Persistent Store */
static const marshal_member_descriptor_t bt_device_pdd_t_member_descriptors[] =
{
    /*! Last set A2dp volume for the device */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint8, a2dp_volume),
    /*! HFP or HSP profile */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint8, hfp_profile),
    /*! Type of device enumeration, DEVICE_TYPE_XXX */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint8, type),
    /*! Device Link mode */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint8, link_mode),
    /*! Bitmap of supported profiles */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint8, reserved_1),
    /*! Bitmap of last connected profiles */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint8, reserved_2),
    /*! Padding */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint16, padding),
    /*! Misc. flags */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint16, flags),
    /*! Supported SCO forwarding features */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint16, sco_fwd_features),
    /*! Left Battery Server client configuration */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint16, battery_server_config_l),
    /*! Right Battery Server client configuration */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint16, battery_server_config_r),
    /*! GATT Server client configuration */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint16, gatt_server_config),
    /*! GATT Server services changed flag */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint8, gatt_server_services_changed),
    /*! Last selected Voice Assistant */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint8, voice_assistant),
    /*! Device used by the device test service */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint8, dts),
    /*! Bitmap of supported profiles */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint32, supported_profiles),
    /*! Bitmap of last connected profiles */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint32, last_connected_profiles),
    /*! Last set Hfp volume for the device */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint8, hfp_volume),
    /*! Last set Hfp mic gain for the device */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint8, hfp_mic_gain),
    /*! Voice assistant device flags */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint8, va_flags),
    /*! Voice Assistant locale */
    MAKE_MARSHAL_MEMBER_ARRAY(bt_device_pdd_t, uint8, va_locale, 4),
    /*! Headset Service config */
    MAKE_MARSHAL_MEMBER(bt_device_pdd_t, uint32, headset_service_config),
} ;

const marshal_type_descriptor_t marshal_type_descriptor_bt_device_pdd_t = MAKE_MARSHAL_TYPE_DEFINITION(bt_device_pdd_t, bt_device_pdd_t_member_descriptors);


