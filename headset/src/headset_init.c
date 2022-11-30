/*!
\copyright  Copyright (c) 2019 - 2021 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version    %%version
\file       headset_init.c
\brief      Initialisation module
*/

#ifndef HEADSET_INIT_C
#define HEADSET_INIT_C

#include "headset_init.h"
#include "headset_config.h"
#include "headset_sm.h"
#include "headset_setup_audio.h"
#include "headset_ui_config.h"
#include "headset_ui.h"
#include "headset_usb.h"
#include "headset_setup_unexpected_message.h"
#include "app_task.h"
#include "headset_wired_audio_controller.h"
#include "headset_temperature_config.h"
#include "headset_region_config.h"

#include "gaia_framework.h"
#include "upgrade_gaia_plugin.h"
#if defined(INCLUDE_GAIA_PYDBG_REMOTE_DEBUG) || defined(INCLUDE_GAIA_PANIC_LOG_TRANSFER)
#include "gaia_debug_plugin.h"
#endif
#include "anc_gaia_plugin.h"
#if defined(INCLUDE_MUSIC_PROCESSING)
#include "music_processing.h"
#include "music_processing_gaia_plugin.h"
#endif
#ifdef ENABLE_GAIA_USER_FEATURE_LIST_DATA
#include "headset_gaia_user_feature_config.h"
#endif

#include "gatt_handler.h"
#include "gatt_connect.h"
#include "gatt_server_battery.h"
#include "gatt_server_gatt.h"
#include "gatt_server_gap.h"

#include "authentication.h"
#include "adk_log.h"
#include "unexpected_message.h"
#include "temperature.h"
#include "handset_service.h"
#include "pairing.h"
#include "power_manager.h"
#include "power_manager_action.h"
#include "gaia.h"
#include "connection_manager_config.h"
#include "device_db_serialiser.h"
#include <device_properties.h>
#include "bt_device_class.h"
#include "link_policy.h"
#include "logical_input_switch.h"
#include "input_event_manager.h"
#include "pio_monitor.h"
#include "local_addr.h"
#include "local_name.h"
#include "connection_message_dispatcher.h"
#include "ui.h"
#include "ui_tones.h"
#include "volume_messages.h"
#include "volume_service.h"
#include "media_player.h"
#include "telephony_service.h"
#include "audio_sources.h"
#include "voice_sources.h"
#include "telephony_messages.h"
#include "headset_topology.h"
#include "aec_leakthrough.h"
#include "le_advertising_manager.h"
#include "headset_phy_state.h"
#include "battery_region.h"

#ifdef INCLUDE_AMA
#include "ama.h"
#endif
#ifdef INCLUDE_AMA_LE
#include "gatt_server_ama.h"
#endif

#include "le_scan_manager.h"
#include "wired_audio_source.h"
#include "single_entity.h"
#include "audio_router.h"
#include "focus_select.h"
#include "gaming_mode.h"

#include "gatt_handler.h"
#include "gatt_connect.h"
#include "gatt_server_battery.h"
#include "gatt_server_gatt.h"
#include "gatt_server_gap.h"
#include "anc_state_manager.h"
#include "aec_leakthrough.h"
#include "audio_curation.h"

#include "headset_buttons.h"

#include "usb_device.h"

#include <bredr_scan_manager.h>
#include <bandwidth_manager.h>
#include <connection_manager.h>
#include <device_list.h>
#include <dfu.h>
#include <hfp_profile.h>
#include <hfp_profile_battery_level.h>
#include <charger_monitor.h>
#include <message_broker.h>
#include <profile_manager.h>
#include <ui_prompts.h>
#include <led_manager.h>
#include <av.h>
#include <ui.h>
#include <ui_leds.h>
#include <multidevice.h>
#include <system_state.h>
#include <fast_pair.h>
#include <tx_power.h>

#include <voice_ui.h>
#include <voice_ui_eq.h>

#include <panic.h>
#include <pio.h>
#include <stdio.h>
#include <feature.h>

#ifdef INCLUDE_SWIFT_PAIR
#include <swift_pair.h>
#endif

#ifdef INCLUDE_GAA
#include <gaa.h>
#include <gaa_ota.h>
#endif

#ifdef INCLUDE_GAA_LE
#include <gatt_server_gaa_comm.h>
#include <gatt_server_ams_proxy.h>
#include <gatt_server_ancs_proxy.h>
#include <gatt_server_gaa_media.h>
#endif

#ifdef INCLUDE_GATT_SERVICE_DISCOVERY
#include <gatt_client.h>
#include <gatt_service_discovery.h>
#endif
#include <device_test_service.h>

#ifdef INCLUDE_ACCESSORY
#include "accessory.h"
#include "request_app_launch.h"
#include "rtt.h"
#endif

#ifdef INCLUDE_QCOM_CON_MANAGER
#include <qualcomm_connection_manager.h>
#endif

#ifdef UNMAP_AFH_CH78
#include <app/bluestack/dm_prim.h>
#endif


#define SELF_DVICE_KEY_SIZE             (0x08)

#define INVALID_LINK_KEY                (0xFFFF)

#ifdef INCLUDE_MUSIC_PROCESSING
    voice_ui_eq_if_t voice_ui_eq_if =
    {
        MusicProcessing_IsEqActive,
        MusicProcessing_GetNumberOfActiveBands,
        MusicProcessing_SetUserEqBands,
        MusicProcessing_SetPreset
    };
#endif /* INCLUDE_MUSIC_PROCESSING */

/*!< Structure used while initialising */
initData    headset_init;

/*! \brief Forward CL_INIT_CFM message to the init task handler. */
static void headsetInitFwdClInitCfm(const CL_INIT_CFM_T * cfm)
{
    CL_INIT_CFM_T *copy = PanicUnlessNew(CL_INIT_CFM_T);
    *copy = *cfm;
    MessageSend(SystemState_GetTransitionTask(), CL_INIT_CFM, copy);
}

/*! \brief Handle Connection library confirmation message */
static void headsetInitHandleClInitCfm(const CL_INIT_CFM_T *cfm)
{
    if (cfm->status != success)
        Panic();

    /* Set the class of device to indicate this is a headset */
    ConnectionWriteClassOfDevice(AUDIO_MAJOR_SERV_CLASS | RENDER_MAJOR_SERV_CLASS |
                                 AV_MAJOR_DEVICE_CLASS | HEADSET_MINOR_DEVICE_CLASS);

    /* Allow SDP without security, requires authorisation */
    ConnectionSmSetSecurityLevel(0, 1, ssp_secl4_l0, TRUE, TRUE, FALSE);

    /* Reset security mode config - always turn off debug keys on power on */
    ConnectionSmSecModeConfig(appGetAppTask(), cl_sm_wae_acl_owner_none, FALSE, TRUE);

    headsetInitFwdClInitCfm(cfm);
}

/*! \brief Handle Connection library confirmation message to store SELF device in PDL */
static bool headsetInitHandleClDmLocalBdAddrCfm(Message message)
{
    CL_DM_LOCAL_BD_ADDR_CFM_T *cfm = (CL_DM_LOCAL_BD_ADDR_CFM_T *)message;
    uint16 self_device_lk[SELF_DVICE_KEY_SIZE] = {INVALID_LINK_KEY};

    if (cfm->status != success)
        Panic();

    /* Store the SELF device in the PDL */
    ConnectionSmAddAuthDevice(&headset_init.task,
                              &cfm->bd_addr,
                              FALSE,
                              TRUE,
                              cl_sm_link_key_unauthenticated_p192,
                              SELF_DVICE_KEY_SIZE,
                              self_device_lk);

    return TRUE;
}

/*! \brief Handle Connection library confirmation message to add SELF BT device */
static void headsetInitHandleClSmAddAuthDeviceCfm(const CL_SM_ADD_AUTH_DEVICE_CFM_T *cfm)
{
    if (cfm->status != success)
    {
        Panic();
    }

    PanicNull(BtDevice_GetDeviceCreateIfNew(&cfm->bd_addr, DEVICE_TYPE_SELF));

    /* Set the protection for SELF device so just in case when trusted device list(appConfigMaxTrustedDevices)
    is exhasuted, connection library will not overwrite entry of this SELF device in the PDL */
    ConnectionAuthSetPriorityDevice(&cfm->bd_addr, TRUE);

    /* Save the self BT device in PS store so that we don't lose any data in case of unexpected power cycle */
    DeviceDbSerialiser_Serialise();
    BtDevice_PrintAllDevices();
}

/*! \brief Connection library Message Handler

    This function is the main message handler for the main application task, every
    message is handled in it's own seperate handler function.  The switch
    statement is broken into seperate blocks to reduce code size, if execution
    reaches the end of the function then it is assumed that the message is
    unhandled.
*/
static void headsetHandleClMessage(Task task, MessageId id, Message message)
{
    UNUSED(task);

    DEBUG_LOG_V_VERBOSE("headsetHandleClMessage called, message id MESSAGE:0x%x", id);
    /* Handle Connection Library messages that are not sent directly to
       the requestor */
    if (CL_MESSAGE_BASE <= id && id < CL_MESSAGE_TOP)
    {
        bool handled = FALSE;

        if (id == CL_INIT_CFM)
        {
            headsetInitHandleClInitCfm((const CL_INIT_CFM_T *)message);
            DEBUG_LOG_DEBUG("headsetHandleClMessage:CL_INIT_CFM Received ");

            return;
        }

        if(id == CL_SM_ADD_AUTH_DEVICE_CFM)
        {
            DEBUG_LOG_VERBOSE("headsetHandleClMessage:CL_SM_ADD_AUTH_DEVICE_CFM Received ");
            headsetInitHandleClSmAddAuthDeviceCfm((const CL_SM_ADD_AUTH_DEVICE_CFM_T *)message);

            return;
        }
        /* Pass connection library messages in turn to the modules that
           are interested in them.
         */
        handled |= Pairing_HandleConnectionLibraryMessages(id, message, handled);
        handled |= ConManagerHandleConnectionLibraryMessages(id, message, handled);
        handled |= appLinkPolicyHandleConnectionLibraryMessages(id, message, handled);
        handled |= appAuthHandleConnectionLibraryMessages(id, message, handled);
        handled |= LeAdvertisingManager_HandleConnectionLibraryMessages(id, message, handled);
        handled |= LocalAddr_HandleConnectionLibraryMessages(id, message, handled);
        handled |= BtDevice_HandleConnectionLibraryMessages(id, message, handled);
#ifdef INCLUDE_FAST_PAIR
        handled |= FastPair_HandleConnectionLibraryMessages(id, message, handled);
#endif
        if (handled)
        {
            return;
        }
    }

    DEBUG_LOG_VERBOSE("headsetHandleClMessage called but unhandled, message id MESSAGE:0x%x", id);
    UnexpectedMessage_HandleMessage(id);
}

/*! \brief Transport manager does not have proper init function, so add a fix for it   */
static bool headsetInitTransportManagerInitFixup(Task init_task)
{
    UNUSED(init_task);

    TransportMgrInit();

    return TRUE;
}

static const bt_device_default_value_callback_t property_default_values[] =
{
        {device_property_headset_service_config, HandsetService_SetDefaultConfig}
};

static const bt_device_default_value_callback_list_t default_value_callback_list = {property_default_values, ARRAY_DIM(property_default_values)};


/*! \brief Utility function to Init device DB serialiser   */
static bool headsetInitDeviceDbSerialiser(Task init_task)
{
    UNUSED(init_task);

    DeviceDbSerialiser_Init();

    BtDevice_RegisterPropertyDefaults(&default_value_callback_list);

    /* Register persistent device data users */
    BtDevice_RegisterPddu();

#ifdef INCLUDE_FAST_PAIR
    FastPair_RegisterPersistentDeviceDataUser();
#endif

    /* Allow space in device list to store all paired devices + connected handsets not yet paired */
    DeviceList_Init(appConfigHeadsetMaxDevicesSupported() + appConfigMaxNumOfHandsetsCanConnect());
    DeviceDbSerialiser_Deserialise();

    return TRUE;
}

/*! \brief Utility function to get input actions  */
static const InputActionMessage_t* headsetInitGetInputActions(uint16* input_actions_dim)
{
    const InputActionMessage_t* input_actions = NULL;

#ifdef HAVE_1_BUTTON
    DEBUG_LOG_VERBOSE("headsetInitGetInputActions media_message_group");
    *input_actions_dim = ARRAY_DIM(media_message_group);
    input_actions = media_message_group;
#else /* HAVE_1_BUTTON */
    *input_actions_dim = ARRAY_DIM(default_message_group);
    input_actions = default_message_group;
#endif  /* HAVE_1_BUTTON */

    return input_actions;
}
/*! \brief Utility function to init event manager   */
static bool headsetInputEventMangerInit(Task init_task)
{
    const InputActionMessage_t* input_actions = NULL;
    uint16 input_actions_dim = 0;
    UNUSED(init_task);

    input_actions = headsetInitGetInputActions(&input_actions_dim);
    PanicNull((void*)input_actions);

    /* Initialise input event manager with auto-generated tables for
    * the target platform. Connect to the logical Input Switch component */
    InputEventManagerInit(LogicalInputSwitch_GetTask(), input_actions,
                            input_actions_dim, &input_event_config);
    return TRUE;
}

/*! \brief Register connection message dispatcher  */
static bool headsetMessageDispatcherRegister(Task init_task)
{
    Task client = &headset_init.task;

    UNUSED(init_task);

    ConnectionMessageDispatcher_RegisterInquiryClient(client);
    ConnectionMessageDispatcher_RegisterCryptoClient(client);
    ConnectionMessageDispatcher_RegisterLeClient(client);
    ConnectionMessageDispatcher_RegisterTdlClient(client);
    ConnectionMessageDispatcher_RegisterL2capClient(client);
    ConnectionMessageDispatcher_RegisterLocalDeviceClient(client);
    ConnectionMessageDispatcher_RegisterPairingClient(client);
    ConnectionMessageDispatcher_RegisterLinkPolicyClient(client);
    ConnectionMessageDispatcher_RegisterTestClient(client);
    ConnectionMessageDispatcher_RegisterRemoteConnectionClient(client);
    ConnectionMessageDispatcher_RegisterRfcommClient(client);
    ConnectionMessageDispatcher_RegisterScoClient(client);
    ConnectionMessageDispatcher_RegisterSdpClient(client);

    return TRUE;
}

/*! \brief Connection library initialisation */
static bool headsetConnectionInit(Task init_task)
{
    static const msg_filter filter = {msg_group_acl | msg_group_mode_change};

    UNUSED(init_task);

    ConnectionMessageDispatcher_Init();

    /* Initialise the Connection Manager */
#if defined(APP_SECURE_CONNECTIONS)
    ConnectionInitEx3(ConnectionMessageDispatcher_GetHandler(), &filter, appConfigHeadsetMaxDevicesSupported(), CONNLIB_OPTIONS_SC_ENABLE);
#else
    ConnectionInitEx3(ConnectionMessageDispatcher_GetHandler(), &filter, appConfigHeadsetMaxDevicesSupported(), CONNLIB_OPTIONS_NONE);
#endif
    ConnectionMessageDispatcher_RegisterInitClient(&headset_init.task);

    return TRUE;
}

/*! \brief Utility function to config the gatt battery server for headset app */
static bool headsetGattServerBatteryConfig(Task init_task)
{
    Multidevice_SetType(multidevice_type_single);
    Multidevice_SetSide(multidevice_side_both);
    GattServerBattery_SetNumberOfBatteryServers(NUMBER_BATTERY_SERVERS_HEADSET);

    UNUSED(init_task);
    return TRUE;
}

/*! \brief Utility to check license for codec and cvc

    This function is used to verify different codec/cVc license are available or not
*/
static bool headsetLicenseCheck(Task init_task)
{
    if (FeatureVerifyLicense(CVC_RECV))
        DEBUG_LOG_VERBOSE("headsetLicenseCheck: cVc Receive is licensed");
    else
        DEBUG_LOG_WARN("headsetLicenseCheck: cVc Receive not licensed");

    if (FeatureVerifyLicense(CVC_SEND_HS_1MIC))
        DEBUG_LOG_VERBOSE("headsetLicenseCheck: cVc Send 1-MIC is licensed");
    else
        DEBUG_LOG_WARN("headsetLicenseCheck: cVc Send 1-MIC not licensed");

    if (FeatureVerifyLicense(CVC_SEND_HS_2MIC_MO))
        DEBUG_LOG_VERBOSE("headsetLicenseCheck: cVc Send 2-MIC is licensed");
    else
        DEBUG_LOG_WARN("headsetLicenseCheck: cVc Send 2-MIC not licensed");

    if (FeatureVerifyLicense(APTX_CLASSIC))
        DEBUG_LOG_VERBOSE("appLicenseCheck: aptX Classic is licensed, aptX A2DP CODEC is enabled");
    else
        DEBUG_LOG_WARN("appLicenseCheck: aptX Classic not licensed, aptX A2DP CODEC is disabled");

    if (FeatureVerifyLicense(APTX_HD))
        DEBUG_LOG_VERBOSE("appLicenseCheck: aptX HD is licensed, aptX A2DP CODEC is enabled");
    else
        DEBUG_LOG_WARN("appLicenseCheck: aptX HD not licensed, aptX HD A2DP CODEC is disabled");

    if (FeatureVerifyLicense(APTX_ADAPTIVE_DECODE))
        DEBUG_LOG_VERBOSE("appLicenseCheck: aptX Adaptive is licensed, aptX Adaptive A2DP CODEC is enabled");
    else
        DEBUG_LOG_WARN("appLicenseCheck: aptX Adaptive not licensed, aptX Adaptive A2DP CODEC is disabled");

    UNUSED(init_task);
    return TRUE;
}

#ifdef UNMAP_AFH_CH78

/*! Unmap AFH channel 78

    It is need to meet regulatory requirements when QHS is used.
*/
static bool headset_RemapAfh78(Task init_task)
{
    static const uint8_t afh_map[10] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f};

    UNUSED(init_task);

    MESSAGE_MAKE(prim, DM_HCI_SET_AFH_CHANNEL_CLASS_REQ_T);
    prim->common.op_code = DM_HCI_SET_AFH_CHANNEL_CLASS_REQ;
    prim->common.length = sizeof(DM_HCI_SET_AFH_CHANNEL_CLASS_REQ_T);
    memcpy(prim->map, afh_map, sizeof(afh_map));

    VmSendDmPrim(prim);

    return TRUE;
}
#endif

/*! \brief Function to initialise wired audio.

    This function is used to initialise and configure wired audio.
*/
static bool headsetWiredAudioInit(Task init_task)
{
   UNUSED(init_task);

   wired_audio_pio_t source_pio;
   source_pio.line_in_pio = WIRED_AUDIO_LINE_IN_PIO;
   WiredAudioSource_Init(&source_pio);

   return TRUE;
}

/*! \brief Function to start creating self device entry.
*/
static bool headsetSelfDeviceInit(Task init_task)
{
    DEBUG_LOG_VERBOSE("headsetSelfDeviceInit");
    ConnectionReadLocalAddr(init_task);
    BtDevice_PrintAllDevices();
    return TRUE;
}

#ifdef INIT_DEBUG
/*! Debug function blocks execution until appInitDebugWait is cleared:
    apps1.fw.env.vars['headsetInitDebugWait'].set_value(0) */
static bool headsetInitDebug(Task init_task)
{
    volatile static bool headsetInitDebugWait = TRUE;
    while(headsetInitDebugWait);

    UNUSED(init_task);
    return TRUE;
}
#endif

#ifdef INCLUDE_FAST_PAIR
static bool appTxPowerInit(Task init_task)
{
    bool result = TxPower_Init(init_task);
    TxPower_SetTxPowerPathLoss(appConfigBoardTxPowerPathLoss);
    return result;
}
#endif


#ifdef INCLUDE_DFU
static bool headsetInit_DfuAppRegister(Task init_task)
{
    UNUSED(init_task);

    DEBUG_LOG_VERBOSE("headsetInit_DfuAppRegister");

    Dfu_ClientRegister(headsetSmGetTask());

    Dfu_SetVersionInfo(UPGRADE_INIT_VERSION_MAJOR, UPGRADE_INIT_VERSION_MINOR, UPGRADE_INIT_CONFIG_VERSION);

    return TRUE;
}

static bool headset_UpgradeGaiaPluginRegister(Task init_task)
{
    UNUSED(init_task);

    DEBUG_LOG("headset_UpgradeGaiaPluginRegister");

    UpgradeGaiaPlugin_Init();

    return TRUE;
}
#endif /* INCLUDE_DFU */

#ifdef ENABLE_ANC
static bool headset_AncGaiaPluginRegister(Task init_task)
{
    UNUSED(init_task);

    DEBUG_LOG_VERBOSE("headset_AncGaiaPluginRegister");

    AncGaiaPlugin_Init();

    return TRUE;
}
#endif

#ifdef INCLUDE_TEMPERATURE
/*! \brief Utility function to Initialise Temperature component   */
static bool headset_TemperatureInit(Task init_task)
{
    const temperature_lookup_t* config_table;
    unsigned config_table_size;

    config_table = HeadsetTemperature_GetConfigTable(&config_table_size);

    /* set voltage->temperature config table */
    Temperature_SetConfigurationTable(config_table, config_table_size);
    
    appTemperatureInit(init_task);
    return TRUE;
}
#endif

/*! \brief Utility function to Initialise Battery Region component   */
static bool headset_BatteryRegionInit(Task init_task)
{
    const charge_region_t* config_table;
    unsigned config_table_size;
    const battery_region_handlers_t* handlers_list;

    UNUSED(init_task);

    config_table = HeadsetRegion_GetChargeModeConfigTable(&config_table_size);

    /* set charge mode config table */
    BatteryRegion_SetChargeRegionConfigTable(CHARGE_MODE, config_table, config_table_size);

    config_table = HeadsetRegion_GetDischargeModeConfigTable(&config_table_size);

    /* set discharge mode config table */
    BatteryRegion_SetChargeRegionConfigTable(DISCHARGE_MODE, config_table, config_table_size);

    /* get handler functions list */
    handlers_list = HeadsetRegion_GetRegionHandlers();

    /* set region state handler functions list */
    BatteryRegion_SetHandlerStructure(handlers_list);
    
    BatteryRegion_Init();
    return TRUE;
}


/*! \brief Table of initialisation functions */
static const system_state_step_t headsetInitTable[] =
{
#ifdef INIT_DEBUG
    {headsetInitDebug,      0, NULL},
#endif
#ifdef ENABLE_AEC_LEAKTHROUGH
    {AecLeakthrough_Init, 0, NULL},
#endif
    {headsetGattServerBatteryConfig,  0, NULL},
    {PioMonitorInit,        0, NULL},
    {Ui_Init,               0, NULL},
    {headsetLicenseCheck,   0, NULL},
#ifdef INCLUDE_TEMPERATURE
    {headset_TemperatureInit,    0, NULL},
#endif
    {appBatteryInit,        MESSAGE_BATTERY_INIT_CFM, NULL},
#ifdef INCLUDE_CHARGER
    {Charger_Init,          0, NULL},
#endif
    {LedManager_Init,       0, NULL},
    {headset_BatteryRegionInit,        0, NULL},
    {appPowerInit,          APP_POWER_INIT_CFM, NULL},    
    {headsetConnectionInit, CL_INIT_CFM, NULL},
#ifdef UNMAP_AFH_CH78
    {headset_RemapAfh78,       0, NULL},
#endif
    {headsetMessageDispatcherRegister, 0, NULL},
    {headsetInputEventMangerInit, 0, NULL},
    {appHeadsetPhyStateInit, HEADSET_PHY_STATE_INIT_CFM, NULL},
    {appLinkPolicyInit,     0, NULL},
    {LocalAddr_Init,        0, NULL},
    {ConManagerInit,        0, NULL},
    {headsetInitDeviceDbSerialiser, 0, NULL},
    {appDeviceInit,         CL_DM_LOCAL_BD_ADDR_CFM, appDeviceHandleClDmLocalBdAddrCfm},
    {headsetSelfDeviceInit, CL_DM_LOCAL_BD_ADDR_CFM, headsetInitHandleClDmLocalBdAddrCfm},
    {BandwidthManager_Init, 0, NULL},
    {BredrScanManager_Init, BREDR_SCAN_MANAGER_INIT_CFM, NULL},
    {LocalName_Init,        LOCAL_NAME_INIT_CFM, NULL},
    {LeAdvertisingManager_Init,     0, NULL},
    {LeScanManager_Init,    0, NULL},
    {AudioSources_Init,     0, NULL},
    {VoiceSources_Init,     0, NULL},
    {Volume_InitMessages,   0, NULL},
    {VolumeService_Init,    0, NULL},
    {appAvInit,             AV_INIT_CFM, NULL},
    {LogicalInputSwitch_Init,0, NULL},
    {Pairing_Init,          PAIRING_INIT_CFM, NULL},
    {FocusSelect_Init,      0, NULL},
    {Telephony_InitMessages,0, NULL},
    {TelephonyService_Init, 0, NULL},
    {HfpProfile_Init,       APP_HFP_INIT_CFM, NULL},
#ifdef INCLUDE_QCOM_CON_MANAGER
    {QcomConManagerInit,    QCOM_CON_MANAGER_INIT_CFM,NULL},
#endif
#ifdef INCLUDE_USB_DEVICE
    {HeadsetUsb_Init,       0, NULL},
#endif
    {appKymeraInit,         0, NULL},
    {SingleEntity_Init,     0, NULL},
    {headsetWiredAudioInit, 0, NULL},
    {MediaPlayer_Init,      0, NULL},
    {headsetInitTransportManagerInitFixup, 0, NULL},        //! \todo TransportManager does not meet expected init interface
    {GattConnect_Init,      0, NULL},   // GATT functionality is initialised by calling GattConnect_Init then GattConnect_ServerInitComplete.
    // All GATT Servers MUST be initialised after GattConnect_Init and before GattConnect_ServerInitComplete.
    {GattHandlerInit,       0, NULL},
#ifdef INCLUDE_GATT_BATTERY_SERVER
    {GattServerBattery_Init,0, NULL},
#endif
    {GattServerGatt_Init,   0, NULL},
    {GattServerGap_Init,    0, NULL},
    {ProfileManager_Init,   0, NULL},
    {HandsetService_Init,   0, NULL},
    {HeadsetTopology_Init,  0, NULL},

#ifdef INCLUDE_GAA_LE
    {GattServerGaaMedia_Init,  0, NULL},
    {GattServerGaaComm_Init,   0, NULL},
    {GattServerAmsProxy_Init,  0, NULL},
    {GattServerAncsProxy_Init, 0, NULL},
#endif
#ifdef INCLUDE_AMA_LE
    {GattServerAma_Init, 0, NULL},
#endif
#ifdef INCLUDE_ACCESSORY
    {Accessory_Init,                        0, NULL},
    {AccessoryFeature_RequestAppLaunchInit, 0, NULL},
    {Rtt_Init,                              0, NULL},
#endif
#ifdef ENABLE_ANC
    {AncStateManager_Init, 0, NULL},
#endif
    {headsetSmInit,         0, NULL},
#if defined(INCLUDE_MUSIC_PROCESSING)
    {MusicProcessing_Init,             0, NULL},
#endif /* INCLUDE_MUSIC_PROCESSING */
#ifdef INCLUDE_GAIA
    {GaiaFramework_Init,   APP_GAIA_INIT_CFM, NULL},   // Gatt needs GAIA
#if defined(INCLUDE_GAIA_PYDBG_REMOTE_DEBUG) || defined(INCLUDE_GAIA_PANIC_LOG_TRANSFER)
    {GaiaDebugPlugin_Init, 0, NULL},
#endif
#if defined(INCLUDE_DFU)
    {headset_UpgradeGaiaPluginRegister, 0, NULL},
#endif
#if defined(ENABLE_ANC)
    {headset_AncGaiaPluginRegister,     0, NULL},
#endif
#if defined(INCLUDE_MUSIC_PROCESSING)
    {MusicProcessingGaiaPlugin_Init,             0, NULL},
#endif
#ifdef ENABLE_GAIA_USER_FEATURE_LIST_DATA
    {HeadsetGaiaUserFeature_RegisterUserFeatureData,  0, NULL},
#endif
#endif /* INCLUDE_GAIA */
#ifdef INCLUDE_DFU
    {Dfu_EarlyInit,   0, NULL},
    {headsetInit_DfuAppRegister, 0, NULL},
    {Dfu_Init,        UPGRADE_INIT_CFM, NULL},
#endif
    {VoiceUi_Init,    0, NULL},
    {UiPrompts_Init,  0, NULL},
    {UiTones_Init,    0, NULL},
    {UiLeds_Init,     0, NULL},
    {HeadsetUi_Init,  0, NULL},
#ifdef INCLUDE_GAA
        {Gaa_Init, 0, NULL},
#endif
    {AudioCuration_Init, 0, NULL},
#ifdef INCLUDE_GAMING_MODE
    {GamingMode_init, 0, NULL},
#endif
#ifdef INCLUDE_FAST_PAIR
    {appTxPowerInit, 0 , NULL},
    {FastPair_Init,  0, NULL},
#endif

#ifdef INCLUDE_GATT_SERVICE_DISCOVERY
    {GattServiceDiscovery_Init, 0, NULL},
#endif
    // All GATT Servers MUST be initialised before GATT initialisation is complete.
    {GattConnect_ServerInitComplete, GATT_CONNECT_SERVER_INIT_COMPLETE_CFM, NULL},

#ifdef INCLUDE_AMA
    {Ama_Init, 0, NULL},
#endif

#ifdef INCLUDE_SWIFT_PAIR
    {SwiftPair_Init, 0, NULL},
#endif
};

/*! \brief Take action to enter dormant mode.*/
static bool finalPowerOffSleepStep(Task task)
{
    UNUSED(task);
    appPowerEnterDormantMode(TRUE);

    return TRUE;
}

/*! \brief Take action to do power off due to emergency shutdown*/
static bool finalEmergencyShutdownStep(Task task)
{
    UNUSED(task);
    appPowerDoPowerOff();

    return TRUE;
}

/*! \brief Take action to prompt power on voice prompt or tone */
static bool headset_trigger_power_on(Task task)
{
    UNUSED(task);
    appPowerOn();

    return TRUE;
}

#ifdef INCLUDE_DFU
/*! \brief Take action to enable DFU upgrades for Headset */
static bool headset_enable_upgrade(Task task)
{
    UNUSED(task);

    return Dfu_AllowUpgrades(TRUE);
}

/*! \brief Take action to enable DFU upgrades for Headset */
static bool headset_disable_upgrade(Task task)
{
    UNUSED(task);

    return Dfu_AllowUpgrades(FALSE);
}
#endif


static const system_state_step_t power_on_table[] =
{
#if !defined (EXCLUDE_POWER_PROMPTS) && defined (INCLUDE_PROMPTS)
    {headset_trigger_power_on, UI_MANDATORY_PROMPT_PLAYBACK_COMPLETED, NULL},
#else
    {headset_trigger_power_on, 0, NULL},
#endif
    {HeadsetTopology_Start, 0, NULL},
#ifdef INCLUDE_WIRED_ANALOG_AUDIO
    {WiredAudioSource_StartMonitoring, 0, NULL},
#endif
#ifdef INCLUDE_USB_DEVICE
    {HeadsetUsb_AudioEnable, 0, NULL},
#endif
#ifdef INCLUDE_DFU
    {headset_enable_upgrade, 0, NULL}
#endif
};

static const system_state_step_t power_off_table[] =
{
    {HeadsetTopology_Stop, HEADSET_TOPOLOGY_STOP_CFM, headetSmHandleTopologyStopCfm},
#ifdef INCLUDE_WIRED_ANALOG_AUDIO
    {WiredAudioSource_StopMonitoring, 0, NULL},
#endif
#ifdef INCLUDE_USB_DEVICE
    {HeadsetUsb_AudioDisable, 0, NULL},
#endif
#ifdef INCLUDE_DFU
    {headset_disable_upgrade, 0, NULL}
#endif
};

static const system_state_step_t shutdown_table[] =
{
    {finalPowerOffSleepStep, 0, NULL}
};

static const system_state_step_t emergency_shutdown_table[] =
{
    {finalEmergencyShutdownStep, 0, NULL}
};

/*! \brief Initialize message broker group registration*/
static void headsetInit_SetMessageBrokerRegistrations(void)
{
    unsigned registrations_array_dim = (unsigned)message_broker_group_registrations_end -
                              (unsigned)message_broker_group_registrations_begin;
    PanicFalse((registrations_array_dim % sizeof(message_broker_group_registration_t)) == 0);
    registrations_array_dim /= sizeof(message_broker_group_registration_t);

    MessageBroker_Init(message_broker_group_registrations_begin,
                       registrations_array_dim);
}

/*! \brief Initialize headset UI */
static void headsetInit_CompleteUiInitialisation(void)
{
    const ui_config_table_content_t* config_table;
    unsigned config_table_size;

    config_table = HeadsetUi_GetConfigTable(&config_table_size);

    Ui_SetConfigurationTable(config_table, config_table_size);
    LogicalInputSwitch_SetLogicalInputIdRange(MIN_INPUT_ACTION_MESSAGE_ID,
                                              MAX_INPUT_ACTION_MESSAGE_ID);

    HeadsetUi_ConfigureFocusSelection();

    /* UI and App is completely initialized, system is ready for inputs */
    PioMonitorEnable();
}

void HeadsetInit_StartInitialisation(void)
{
    headset_init.task.handler = headsetHandleClMessage;

    DEBUG_LOG_VERBOSE("HeadsetInit_StartInitialisation: Start Initialising");

    headsetInit_SetMessageBrokerRegistrations();

    LedManager_SetHwConfig(&headset_led_config);

    headsetSetBundlesConfig();

    SystemState_Init();

    SystemState_RegisterForStateChanges(appGetAppTask());
    SystemState_RegisterTableForInitialise(headsetInitTable, ARRAY_DIM(headsetInitTable));
    SystemState_RegisterTableForPowerOn(power_on_table, ARRAY_DIM(power_on_table));
    SystemState_RegisterTableForPowerOff(power_off_table, ARRAY_DIM(power_off_table));
    SystemState_RegisterTableForShutdown(shutdown_table, ARRAY_DIM(shutdown_table));
    SystemState_RegisterTableForEmergencyShutdown(emergency_shutdown_table, ARRAY_DIM(emergency_shutdown_table));

#ifdef INCLUDE_GAA
    Gaa_OtaSetSilentCommitSupported(UPGRADE_SILENT_COMMIT_SUPPORTED);
#endif
    SystemState_Initialise();
}

void HeadsetInit_CompleteInitialisation(void)
{
    headsetInit_CompleteUiInitialisation();

    HeadsetWiredAudioController_Init();

    /* complete power manager initialisation*/
    appPowerInitComplete();

    Headset_SetupUnexpectedMessage();

    headsetSetupAudio();

#ifdef INCLUDE_GAA
    Gaa_InitComplete();
#endif

#ifdef INCLUDE_MUSIC_PROCESSING
    VoiceUi_SetEqInterface(&voice_ui_eq_if);
#endif

#if defined(INCLUDE_GAIA) && defined(INCLUDE_DFU)
    DEBUG_LOG_VERBOSE("Registration of SmGetTask() with GAIA");

    GaiaFrameworkInternal_ClientRegister(headsetSmGetTask());
#endif

    HfpProfile_BatteryLevelInit();

#ifdef INCLUDE_DFU
    Dfu_SetSilentCommitSupported(UPGRADE_SILENT_COMMIT_SUPPORTED);
#endif

#ifdef ENABLE_LE_ADVERTISING_NO_RESTART_ON_DATA_UPDATE
    LeAdvertisingManager_ConfigureAdvertisingOnNotifyDataChange(le_adv_config_notify_keep_advertising);
#endif

    SystemState_StartUp();
    DEBUG_LOG_INFO("HeadsetInit_CompleteInitialisation:Completed Initialisation");
}

#endif /* HEADSET_INIT_C */
