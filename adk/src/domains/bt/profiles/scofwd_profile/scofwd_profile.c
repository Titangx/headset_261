/*!
\copyright  Copyright (c) 2008 - 2020 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\file
\brief      Implementation of SCO forwarding.

            This file covers management of a single L2CAP link to the peer device
            and transmission of data over the link.

            There is some interaction with \ref kymera.c, and
            \ref av_headset_con_manager.c.
*/
#define SFWD_DEBUG

#include "scofwd_profile.h"

#include <panic.h>
#include <ps.h>
#include <system_clock.h>
#include <rtime.h>
#include <bluestack/hci.h>
#include <bluestack/l2cap_prim.h>
#include <device.h>
#include <device_properties.h>
#include <util.h>
#include <service.h>
#include <stdlib.h>
#include "a2dp_profile_audio.h"
#include "sdp.h"
#include "domain_message.h"
#include "scofwd_profile_config.h"
#include "scofwd_profile_typedef.h"
#include "scofwd_profile_marshal_typedef.h"
#include "peer_signalling.h"
#include "link_policy.h"
#include "multidevice.h"
#include "system_state.h"
#include "app_task.h"
#include "kymera_adaptation.h"
#include "kymera.h"
#include "phy_state.h"
#include "ui.h"
#include "telephony_messages.h"
#include "voice_sources.h"
#include "volume_messages.h"

#include <logging.h>
#include <state_proxy.h>

#ifndef HOSTED_TEST_ENVIRONMENT

/*! There is checking that the messages assigned by this module do
not overrun into the next module's message ID allocation */
ASSERT_MESSAGE_GROUP_NOT_OVERFLOWED(SFWD, SFWD_MESSAGE_END)

#endif

/* Make the type used for message IDs available in debug tools */
LOGGING_PRESERVE_MESSAGE_ENUM(scofwd_messages)
PRESERVE_ENUM_FOR_DEBUGGING(scofwd_ota_messages)

scoFwdTaskData sco_fwd;
#define GetScoFwd()      (&sco_fwd)
#define GetScoFwdTask()  (&(GetScoFwd()->task))

#define MAKE_SFWD_MESSAGE(TYPE) TYPE##_T *message = PanicUnlessNew(TYPE##_T);

/* This is the maximum number of messages taken to suspend A2DP if it was active
    when SCO forwarding receive was received. Message is resent if av is streaming.

    AV_INTERNAL_A2DP_SUSPEND_MEDIA_REQ
    KYMERA_INTERNAL_A2DP_STOP
 */
#define SFWD_SCO_RX_START_MSG_DELAY 2



#define SCOFWD_TRANS_DEBUG
#ifdef SCOFWD_TRANS_DEBUG
uint16 ScoFwd_trans_debug_limit = 10;
uint16 ScoFwd_trans_debug_tx_count = 0;
uint16 ScoFwd_trans_debug_rx_count = 0;
#define SCOFWD_TRANS_TX_DEBUG_LOG(...)         if (ScoFwd_trans_debug_tx_count == ScoFwd_trans_debug_limit-1) { \
                                                   DEBUG_LOG(__VA_ARGS__); \
                                               } \
                                               ScoFwd_trans_debug_tx_count++; \
                                               ScoFwd_trans_debug_tx_count = ScoFwd_trans_debug_tx_count % ScoFwd_trans_debug_limit;

#define SCOFWD_TRANS_RX_DEBUG_LOG(...)         if (ScoFwd_trans_debug_rx_count == ScoFwd_trans_debug_limit-1) { \
                                                   DEBUG_LOG(__VA_ARGS__); \
                                               } \
                                               ScoFwd_trans_debug_rx_count++; \
                                               ScoFwd_trans_debug_rx_count = ScoFwd_trans_debug_rx_count % ScoFwd_trans_debug_limit;

#else   /* SCOFWD_TRANS_DEBUG */
#define SCOFWD_TRANS_TX_DEBUG_LOG(...)         DEBUG_LOG(__VA_ARGS__)
#define SCOFWD_TRANS_RX_DEBUG_LOG(...)         DEBUG_LOG(__VA_ARGS__)
#endif  /* SCOFWD_TRANS_DEBUG */

#define SCO_METADATA_OK                 0
#define SCO_METADATA_NOTHING_RECEIVED   2

#define GetScoFwdStats()     (&(GetScoFwd()->stats))
#define ScoFwdGetState()     GetScoFwd()->state

static void ScoFwdProcessReceivedAirPacket(uint16 avail);
void ttp_stats_print(void);

/* SCO TTP Management functions
 The following are used for recording a received TTP, finding the next one
 and identifying any duplicates from late packets / race conditions.
*/
static void clear_last_received_ttp(void);

/* Functions to manage packets arriving late.
   A timer is set for an interval before the expected TTP
   such that we have time to trigger a fake packet.
 */
static void cancel_late_packet_timer(void);

static void ScoFwdSetState(scoFwdState new_state);

static unsigned ScoFwd_GetCurrentContext(void);

#define SHORT_TTP(x) (((x)/1000)%1000)

#define SFWD_NO_RECEIVED_AUDIO(scofwd) ((scofwd)->lost_packets == 32)

#define ALL_SINKS_CONFIGURED(scofwd) ((scofwd)->link_sink && (scofwd)->forwarded_sink)
#define ALL_SOURCES_CONFIGURED(scofwd) ((scofwd)->link_source && (scofwd)->forwarding_source)

#define ScoFwdInActiveState() (   SFWD_STATE_CONNECTED_ACTIVE_RECEIVE == ScoFwdGetState() \
                                  || SFWD_STATE_CONNECTED_ACTIVE_SEND == ScoFwdGetState())

/*! Macro for creating messages */
#define MAKE_SFWD_INTERNAL_MESSAGE(TYPE) \
    SFWD_INTERNAL_##TYPE##_T *message = PanicUnlessNew(SFWD_INTERNAL_##TYPE##_T);


/*! \brief Internal message IDs */
typedef enum
{
    SFWD_INTERNAL_BASE,
    SFWD_INTERNAL_LINK_CONNECT_REQ = SFWD_INTERNAL_BASE,
    SFWD_INTERNAL_LINK_DISCONNECT_REQ,
    SFWD_INTERNAL_START_RX_CHAIN,
    SFWD_INTERNAL_STOP_RX_CHAIN,
    SFWD_INTERNAL_KICK_PROCESSING,
    SFWD_INTERNAL_MIC_CHAIN_DETAILS,
    SFWD_INTERNAL_RX_AUDIO_MISSING,
    SFWD_INTERNAL_ENABLE_FORWARDING,
    SFWD_INTERNAL_DISABLE_FORWARDING,
    SFWD_INTERNAL_PLAY_RING,
    SFWD_INTERNAL_ROLE_NOTIFY,

    SFWD_TIMER_LATE_PACKET,

    /*! This must be the final message */
    SFWD_INTERNAL_MESSAGE_END
} ScoFwdInternalMessageId_t;
ASSERT_INTERNAL_MESSAGES_NOT_OVERFLOWED(SFWD_INTERNAL_MESSAGE_END)
LOGGING_PRESERVE_MESSAGE_TYPE(ScoFwdInternalMessageId_t)

typedef struct
{
    Sink    sink;
} SFWD_INTERNAL_CHAIN_DETAILS_T;

typedef struct
{
    hci_role    role;
} SFWD_INTERNAL_ROLE_NOTIFY_T;

typedef SFWD_INTERNAL_CHAIN_DETAILS_T SFWD_INTERNAL_MIC_CHAIN_DETAILS_T;
#ifndef INCLUDE_SCOFWD_TTP_STATS_PRINT
void ttp_stats_print(void)
{
    DEBUG_LOG("TTP STATS Not Enabled");
}
#define ttp_stats_add(x)
#else
#define TTP_STATS_RANGE         (appConfigScoFwdVoiceTtpMs())
#define TTP_STATS_NUM_CELLS     20
#define TTP_STATS_CELL_SIZE     (TTP_STATS_RANGE / TTP_STATS_NUM_CELLS)
#define TTP_STATS_MIN_VAL       (  appConfigScoFwdVoiceTtpMs() \
                                 - US_TO_MS(SFWD_RX_PROCESSING_TIME_NORMAL_US) \
                                 - TTP_STATS_NUM_CELLS * TTP_STATS_CELL_SIZE)
#define TTP_STATS_MAX_VAL       (TTP_STATS_MIN_VAL + TTP_STATS_CELL_SIZE * TTP_STATS_NUM_CELLS)

typedef struct {
    uint32 entries;
    uint32 sum;
} TTP_STATS_CELL;
TTP_STATS_CELL ttp_stats[TTP_STATS_NUM_CELLS+2] = {0};


static void ttp_stats_add(unsigned ttp_in_future_ms)
{
    int cell;

    if (ttp_in_future_ms < TTP_STATS_MIN_VAL)
    {
        cell = TTP_STATS_NUM_CELLS;
    }
    else if (ttp_in_future_ms >= TTP_STATS_MAX_VAL)
    {
        cell = TTP_STATS_NUM_CELLS + 1;
    }
    else
    {
        cell = (ttp_in_future_ms - TTP_STATS_MIN_VAL) / TTP_STATS_CELL_SIZE;
        if (cell > TTP_STATS_NUM_CELLS + 1)
            Panic();
    }

    ttp_stats[cell].entries++;
    ttp_stats[cell].sum += ttp_in_future_ms;
}

static void ttp_stats_print_cell(unsigned minval,unsigned maxval,unsigned entries,unsigned average)
{
    UNUSED(minval);
    UNUSED(maxval);
    UNUSED(entries);
    UNUSED(average);
    DEBUG_LOG("\t%4d,%-4d,\t%6d,\t%4d",minval,maxval,entries,average);
}

void ttp_stats_print(void)
{
    int cell;

    DEBUG_LOG("TTP STATS");
    DEBUG_LOG("\tCELL RANGE,\tNum,\tAverage");

    for (cell = 0;cell < TTP_STATS_NUM_CELLS;cell++)
    {
        unsigned minval = TTP_STATS_MIN_VAL + (cell * TTP_STATS_CELL_SIZE);
        unsigned maxval = minval + TTP_STATS_CELL_SIZE - 1;
        unsigned average = ttp_stats[cell].entries ? ((ttp_stats[cell].sum + ttp_stats[cell].entries/2)/ttp_stats[cell].entries) : 0;
        ttp_stats_print_cell(minval,maxval,ttp_stats[cell].entries,average);
    }
    if (ttp_stats[cell].entries)
    {
        ttp_stats_print_cell(-1000,TTP_STATS_MIN_VAL -1,ttp_stats[cell].entries,(ttp_stats[cell].sum + ttp_stats[cell].entries/2)/ttp_stats[cell].entries);
    }
    cell++;
    if (ttp_stats[cell].entries)
    {
        ttp_stats_print_cell(TTP_STATS_MAX_VAL+1,1000,ttp_stats[cell].entries,(ttp_stats[cell].sum + ttp_stats[cell].entries/2)/ttp_stats[cell].entries);
    }
}
#endif


/* Track good/bad packets */
static void clearPacketStats(void)
{
    scoFwdReceivedPacketStats *stats = GetScoFwdStats();

    stats->lost_packets = 0;
    stats->packet_history = 0;
    stats->audio_missing = FALSE;
}

static void cancel_late_packet_timer(void)
{
    uint16 cancel_count = MessageCancelAll(GetScoFwdTask(), SFWD_TIMER_LATE_PACKET);

    if (cancel_count  > 1)
    {
        DEBUG_LOG("cancel_late_packet_timer. More than one time cancelled - %d ???",cancel_count);
    }
}

static void clear_last_received_ttp(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();

    theScoFwd->ttp_of_last_received_packet = 0;
}

static void SendOTAControlMessage(uint8 ota_msg_id)
{
    scofwd_profile_ota_message_ind_t *ind = PanicUnlessMalloc(sizeof(*ind));
    ind->ota_msg_id = ota_msg_id;
    appPeerSigMarshalledMsgChannelTx(GetScoFwdTask(),
                                     PEER_SIG_MSG_CHANNEL_SCOFWD,
                                     ind, MARSHAL_TYPE(scofwd_profile_ota_message_ind_t));
}

static void ScoFwdProcessForwardedAudio(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    uint16 avail;

    while ((avail = SourceBoundary(theScoFwd->link_source)) != 0)
    {
        ScoFwdProcessReceivedAirPacket(avail);
    }
}

static void ProcessOTAControlMessage(uint8 ota_msg_id)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();

    DEBUG_LOG("ProcessOTAControlMessage. OTA message ID enum:scofwd_ota_messages:0x%02X",ota_msg_id);

    switch (ota_msg_id)
    {
        case SFWD_OTA_MSG_SETUP:
            MessageSend(GetScoFwdTask(), SFWD_INTERNAL_START_RX_CHAIN, NULL);
            break;

        case SFWD_OTA_MSG_TEARDOWN:
            MessageSend(GetScoFwdTask(), SFWD_INTERNAL_STOP_RX_CHAIN, NULL);
            break;

        case SFWD_OTA_MSG_MIC_SETUP:
            DEBUG_LOG("Received indication that MIC set up remotely");
            ScoFwdMicForwardingEnable(KymeraGetTaskData()->mic == MIC_SELECTION_REMOTE);
            break;

        case SFWD_OTA_MSG_INCOMING_CALL:
            DEBUG_LOG("SCO Forwarding notified of incoming call");
            theScoFwd->peer_incoming_call = TRUE;
            break;

        case SFWD_OTA_MSG_INCOMING_ENDED:
            DEBUG_LOG("SCO Forwarding notified of incoming call END");
            theScoFwd->peer_incoming_call = FALSE;
            break;

        case SFWD_OTA_MSG_CALL_ANSWER:
            DEBUG_LOG("SCO Forwarding PEER ANSWERING call");
            break;

        case SFWD_OTA_MSG_CALL_REJECT:
            DEBUG_LOG("SCO Forwarding PEER REJECTING call");
            break;

        case SFWD_OTA_MSG_CALL_HANGUP:
            DEBUG_LOG("SCO Forwarding PEER ending call");
            break;

        case SFWD_OTA_MSG_CALL_VOICE:
            DEBUG_LOG("SCO Forwarding PEER call voice");
            break;

        case SFWD_OTA_MSG_MICFWD_START:
            DEBUG_LOG("MIC Forwarding Start");
            appKymeraScoForwardingPause(FALSE);
            break;

        case SFWD_OTA_MSG_MICFWD_STOP:
            DEBUG_LOG("MIC Forwarding Stop");
            appKymeraScoForwardingPause(TRUE);
            break;

        default:
            DEBUG_LOG("Unhandled OTA");
            Panic();
            break;
    }
}


/* This function does basic analysis on a packet received over the
   air. This can be a command, or include multiple SCO frames

   Guaranteed to consume 'avail' octets from the source.
   */
static void ScoFwdProcessReceivedAirPacket(uint16 avail)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    Source air_source = theScoFwd->link_source;

    if (!theScoFwd->forwarded_sink)
    {
        DEBUG_LOG("No sink at present");
    }
    else if (avail < SFWD_STRIPPED_AUDIO_FRAME_OCTETS)
    {
        DEBUG_LOG("Too little data for a packet %d < %d",avail,SFWD_STRIPPED_AUDIO_FRAME_OCTETS);
        Panic();
    }

    SourceDrop(air_source,avail);
}

/*! Send connect confirmation to all connect req clients and clean up list. */
static void scoFwd_SendConnectClientsCfm(sfwd_status status)
{
    MAKE_SFWD_MESSAGE(SFWD_CONNECT_CFM);
    message->status = status;
    TaskList_MessageSend(TaskList_GetFlexibleBaseTaskList(ScoFwdGetConnectReqTasks()), SFWD_CONNECT_CFM, message);
    TaskList_RemoveAllTasks(TaskList_GetFlexibleBaseTaskList(ScoFwdGetConnectReqTasks()));
}

/*! Send connect confirmation to all connect req clients and clean up list. */
static void scoFwd_SendDisconnectClientsCfm(sfwd_status status)
{
    MAKE_SFWD_MESSAGE(SFWD_DISCONNECT_CFM);
    message->status = status;
    TaskList_MessageSend(TaskList_GetFlexibleBaseTaskList(ScoFwdGetDisonnectReqTasks()), SFWD_DISCONNECT_CFM, message);
    TaskList_RemoveAllTasks(TaskList_GetFlexibleBaseTaskList(ScoFwdGetDisonnectReqTasks()));
}

static bool ScoFwdStateCanConnect(void)
{
    return ScoFwdGetState() == SFWD_STATE_IDLE;
}

void ScoFwdClearForwarding(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();

    if (theScoFwd->forwarding_source)
    {
        PanicFalse(SourceUnmap(theScoFwd->forwarding_source));
        theScoFwd->forwarding_source = NULL;
    }
}

void ScoFwdClearForwardingReceive(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();

    /* Unmap the timestamped endpoint, as the audio/apps0 won't */
    if (theScoFwd->forwarded_sink)
    {
        PanicFalse(SinkUnmap(theScoFwd->forwarded_sink));
        theScoFwd->forwarded_sink = NULL;
    }

    clear_last_received_ttp();
    clearPacketStats();
}

static void ScoFwdEnterIdle(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();

    DEBUG_LOG("ScoFwdEnterIdle");

    theScoFwd->link_sink = (Sink)NULL;
    theScoFwd->link_source = (Source)NULL;
}

static void ScoFwdExitIdle(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();

    DEBUG_LOG("ScoFwdExitIdle");

    theScoFwd->pending_connects = 0;
}

static void ScoFwdEnterSdpSearch(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    bdaddr peer_bd_addr;

    DEBUG_LOG("ScoFwdEnterSdpSearch");
    PanicFalse(appDeviceGetSecondaryBdAddr(&peer_bd_addr));

    /* Perform SDP search */
    ConnectionSdpServiceSearchAttributeRequest(&theScoFwd->task, &peer_bd_addr, 0x32,
                                               appSdpGetScoFwdServiceSearchRequestSize(), appSdpGetScoFwdServiceSearchRequest(),
                                               appSdpGetScoFwdAttributeSearchRequestSize(), appSdpGetScoFwdAttributeSearchRequest());
}

static void ScoFwdConnectL2cap(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    bdaddr peer_bd_addr;

    static const uint16 l2cap_conftab[] =
    {
        /* Configuration Table must start with a separator. */
            L2CAP_AUTOPT_SEPARATOR,
        /* Flow & Error Control Mode. */
            L2CAP_AUTOPT_FLOW_MODE,
        /* Set to Basic mode with no fallback mode */
                BKV_16_FLOW_MODE( FLOW_MODE_BASIC, 0 ),
        /* Local MTU exact value (incoming). */
            L2CAP_AUTOPT_MTU_IN,
        /*  Exact MTU for this L2CAP connection - 672. */
                672,
        /* Remote MTU Minumum value (outgoing). */
            L2CAP_AUTOPT_MTU_OUT,
        /*  Minimum MTU accepted from the Remote device. */
                48,
         /* Local Flush Timeout  - Accept Non-default Timeout*/
            L2CAP_AUTOPT_FLUSH_OUT,
                BKV_UINT32R(SFWD_FLUSH_MIN_US,SFWD_FLUSH_MAX_US),
            L2CAP_AUTOPT_FLUSH_IN,
                BKV_UINT32R(SFWD_FLUSH_MIN_US,SFWD_FLUSH_MAX_US),

        /* Configuration Table must end with a terminator. */
            L2CAP_AUTOPT_TERMINATOR
    };

    DEBUG_LOG("ScoFwdEnterConnectingMaster");
    PanicFalse(appDeviceGetSecondaryBdAddr(&peer_bd_addr));

    ConnectionL2capConnectRequest(GetScoFwdTask(),
                                  &peer_bd_addr,
                                  theScoFwd->local_psm, theScoFwd->remote_psm,
                                  CONFTAB_LEN(l2cap_conftab),
                                  l2cap_conftab);

    theScoFwd->pending_connects++;
}

/*! \brief Set flag to indicate SCOFWD was connected or not

    \param bd_addr Pointer to read-only device BT address.
    \param connected TRUE if SCOFWD was connected, FALSE otherwise.
*/
static void scoFwdProfile_SetScoFwdWasConnected(const bdaddr *bd_addr, bool connected)
{
    device_t device = BtDevice_GetDeviceForBdAddr(bd_addr);
    if (device)
    {
        uint32 connected_profiles = 0;
        Device_GetPropertyU32(device, device_property_last_connected_profiles, &connected_profiles);

        DEBUG_LOG("scoFwdProfile_SetScoFwdWasConnected, connected %02x", connected_profiles);
        connected_profiles &= ~DEVICE_PROFILE_SCOFWD;
        if (connected)
        {
            connected_profiles |= DEVICE_PROFILE_SCOFWD;
        }
        Device_SetPropertyU32(device, device_property_last_connected_profiles, connected_profiles);
        DEBUG_LOG("scoFwdProfile_SetScoFwdWasConnected, connected %02x", connected_profiles);
    }
}

static void ScoFwdEnterConnected(void)
{
    bdaddr peer_addr;

    DEBUG_LOG("ScoFwdEnterConnected");

    if (appDeviceGetSecondaryBdAddr(&peer_addr))
        scoFwdProfile_SetScoFwdWasConnected(&peer_addr, TRUE);

}

static void ScoFwdExitConnected(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();

    DEBUG_LOG("ScoFwdExitConnected");

    /* cancel any SDP search that may have been started */
    ConnectionSdpTerminatePrimitiveRequest(&theScoFwd->task);
}

static void ScoFwdEnterConnectedActive(void)
{
    DEBUG_LOG("ScoFwdEnterConnectedActive");
}

static void ScoFwdWallclockEnable(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    PanicFalse(RtimeWallClockEnable(&theScoFwd->wallclock, theScoFwd->link_sink));
}

static void ScoFwdWallclockDisable(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    PanicFalse(RtimeWallClockDisable(&theScoFwd->wallclock));
}

static void ScoFwdEnterConnectedActiveSendPendingRoleInd(void)
{
    DEBUG_LOG("ScoFwdEnterConnectedActiveSendPendingRoleInd");
}

/*! \brief Determine if MIC forwarding with peer is supported.

    \return bool TRUE if MIC forwarding is supported.
*/
static bool scoFwdProfile_IsPeerMicForwardSupported(void)
{
    return TRUE;
    /*! \todo Modified for VMCSA-808 Application modifications to Bluetooth Address Management demo.
     *  return TRUE to queries about SCO and MICFWD support, as LE Peer Pairing doesn't do SDP search to set this up correctly.  */
#if 0
    if (appConfigScoForwardingEnabled() && appConfigMicForwardingEnabled())
    {
        bdaddr bd_addr = {0};
        device_t device = BtDevice_GetDeviceForBdAddr(&bd_addr);
        if (device)
        {
            uint16 sco_fwd_features = Device_SetPropertyU16(device, device_property_sco_fwd_features);
            return sco_fwd_features & SFWD_FEATURE_MIC_FWD;
        }
        else
            return FALSE;
    }

    /* We don't support MIC forwarding, so it doesn't matter if the peer does or not */
    return FALSE;
#endif
}

static void ScoFwdEnterConnectedActiveSend(void)
{
    DEBUG_LOG("ScoFwdEnterConnectedActiveSend");

    clear_last_received_ttp();
    clearPacketStats();

    /* Get slave into active receive */
    SendOTAControlMessage(SFWD_OTA_MSG_SETUP);

    /* Enable wallclock for SCO packet timestamps */
    ScoFwdWallclockEnable();

    /* Tell kymera to startup the forwarding components of the SCO chain */
    appKymeraScoStartForwarding(ScoFwdGetSink(),
                                scoFwdProfile_IsPeerMicForwardSupported());
}

static void ScoFwdExitConnectedActiveSend(void)
{
    DEBUG_LOG("ScoFwdExitConnectedActiveSend");

    /* Tell slave to stop, move it back to connected state */
    SendOTAControlMessage(SFWD_OTA_MSG_TEARDOWN);

    /* Tell kymera to stop the forwarding components of the SCO chain */
    appKymeraScoStopForwarding();

    /* Disable wallclock, not needed anymore */
    ScoFwdWallclockDisable();
}

static void ScoFwdEnterConnectedActiveReceive(void)
{
    DEBUG_LOG("ScoFwdEnterConnectedActiveReceive");

    appAvStreamingSuspend(AV_SUSPEND_REASON_SCOFWD);

    /* Enable wallclock for SCO packet timestamps */
    ScoFwdWallclockEnable();


    clear_last_received_ttp();
}

static void ScoFwdExitConnectedActiveReceive(void)
{
    DEBUG_LOG("ScoFwdExitConnectedActiveReceive");

    appKymeraScoSlaveStop();

    /* Clean up local SCO forwarding slave forwarding state */
    ScoFwdClearForwardingReceive();
    if (appConfigMicForwardingEnabled())
    {
        ScoFwdClearForwarding();
    }

    appAvStreamingResume(AV_SUSPEND_REASON_SCOFWD);

    /* Disable wallclock, not needed anymore */
    ScoFwdWallclockDisable();
}

static void ScoFwdEnterDisconnecting(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    DEBUG_LOG("ScoFwdEnterDisconnecting");

    ConnectionL2capDisconnectRequest(GetScoFwdTask(),
                                     theScoFwd->link_sink);
}

static void ScoFwdExitInitialising(void)
{
    MessageSend(SystemState_GetTransitionTask(), SFWD_INIT_CFM, NULL);
}

/*! \brief Set the SCO forwarding FSM state

    Called to change state.  Handles calling the state entry and exit
    functions for the new and old states.
*/
static void ScoFwdSetState(scoFwdState new_state)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    bdaddr peer;
    scoFwdState old_state = theScoFwd->state;

    DEBUG_LOG("ScoFwdSetState(%d) from %d", new_state, old_state);

    switch (old_state)
    {
        case SFWD_STATE_NULL:
        case SFWD_STATE_SDP_SEARCH:
        case SFWD_STATE_CONNECTING:
        case SFWD_STATE_DISCONNECTING:
            break;

        case SFWD_STATE_IDLE:
            ScoFwdExitIdle();
            break;

        case SFWD_STATE_CONNECTED_ACTIVE_RECEIVE:
            ScoFwdExitConnectedActiveReceive();
            break;

        case SFWD_STATE_CONNECTED_ACTIVE_SEND:
            ScoFwdExitConnectedActiveSend();
            break;

        case SFWD_STATE_INITIALISING:
            ScoFwdExitInitialising();
            break;

        default:
            break;
    }

    if (((old_state & ~SFWD_STATE_LOCK_MASK) >= SFWD_STATE_CONNECTED) &&
        ((new_state & ~SFWD_STATE_LOCK_MASK) <  SFWD_STATE_CONNECTED))
    {
        ScoFwdExitConnected();
    }

    /* Set new state */
    theScoFwd->state = new_state;
    theScoFwd->lock = new_state & SFWD_STATE_LOCK_MASK;

    if (((new_state & ~SFWD_STATE_LOCK_MASK) >= SFWD_STATE_CONNECTED) &&
        ((old_state & ~SFWD_STATE_LOCK_MASK) <  SFWD_STATE_CONNECTED))
    {
        ScoFwdEnterConnected();
    }

    switch (new_state)
    {
        case SFWD_STATE_IDLE:
            ScoFwdEnterIdle();
            break;

        case SFWD_STATE_NULL:
            DEBUG_LOG("ScoFwdSetState, null");
            break;

        case SFWD_STATE_INITIALISING:
            DEBUG_LOG("ScoFwdSetState, initialising");
            break;

        case SFWD_STATE_SDP_SEARCH:
            ScoFwdEnterSdpSearch();
            break;

        case SFWD_STATE_CONNECTING:
            DEBUG_LOG("ScoFwdSetState, connecting");
            break;

        case SFWD_STATE_CONNECTED_ACTIVE:
            ScoFwdEnterConnectedActive();
            break;

        case SFWD_STATE_CONNECTED_ACTIVE_SEND:
            ScoFwdEnterConnectedActiveSend();
            break;

        case SFWD_STATE_CONNECTED_ACTIVE_SEND_PENDING_ROLE_IND:
            ScoFwdEnterConnectedActiveSendPendingRoleInd();
            break;

        case SFWD_STATE_CONNECTED_ACTIVE_RECEIVE:
            ScoFwdEnterConnectedActiveReceive();
            break;

        case SFWD_STATE_DISCONNECTING:
            ScoFwdEnterDisconnecting();
            break;

        default:
            break;
    }

    if (appDeviceGetSecondaryBdAddr(&peer))
    {
        appLinkPolicyUpdatePowerTable(&peer);
    }
}

void ScoFwdInitScoPacketising(Source audio_source)
{
    UNUSED(audio_source);
    ConnectionWriteFlushTimeout(ScoFwdGetSink(), SFWD_FLUSH_TARGET_SLOTS);
}

void ScoFwdInitMicPacketising(Source audio_source)
{
    UNUSED(audio_source);
}

void ScoFwdMicForwardingEnable(bool enable)
{
    if (enable)
    {
        SendOTAControlMessage(SFWD_OTA_MSG_MICFWD_START);
    }
    else
    {
        SendOTAControlMessage(SFWD_OTA_MSG_MICFWD_STOP);
    }
}

void ScoFwdNotifyRole(hci_role role)
{
    MAKE_SFWD_INTERNAL_MESSAGE(ROLE_NOTIFY);
    message->role = role;
    MessageSend(GetScoFwdTask(), SFWD_INTERNAL_ROLE_NOTIFY, message);
}

void ScoFwdNotifyIncomingSink(Sink sco_sink)
{
    UNUSED(sco_sink);
}

void ScoFwdNotifyIncomingMicSink(Sink mic_sink)
{
    MAKE_SFWD_INTERNAL_MESSAGE(MIC_CHAIN_DETAILS);

    message->sink = mic_sink;
    MessageSend(GetScoFwdTask(),SFWD_INTERNAL_MIC_CHAIN_DETAILS,message);
}

/* We have a request to make sure that the L2CAP link to our peer
   is established.

   If the link is *not* already up, take steps to connect it */
static void ScoFwdHandleLinkConnectReq(void)
{
    if (ScoFwdStateCanConnect())
        ScoFwdSetState(SFWD_STATE_SDP_SEARCH);
}

static void ScoFwdHandleLinkDisconnectReq(void)
{
    switch (ScoFwdGetState())
    {
        /*! \todo not all non-connected states handled here */
        case SFWD_STATE_CONNECTED_ACTIVE_SEND:
        case SFWD_STATE_CONNECTED_ACTIVE_RECEIVE:
        case SFWD_STATE_CONNECTED:
            ScoFwdSetState(SFWD_STATE_DISCONNECTING);
            break;

        default:
            scoFwd_SendDisconnectClientsCfm(sfwd_status_success);
            break;
    }
}


static void ScoFwdHandleL2capRegisterCfm(const CL_L2CAP_REGISTER_CFM_T *cfm)
{
    DEBUG_LOG("ScoFwdHandleL2capRegisterCfm, status %u, psm %u", cfm->status, cfm->psm);
    PanicFalse(ScoFwdGetState() == SFWD_STATE_INITIALISING);

    /* We have registered the PSM used for SCO forwarding links with
       connection manager, now need to wait for requests to process
       an incoming connection or make an outgoing connection. */
    if (success == cfm->status)
    {
        scoFwdTaskData *theScoFwd = GetScoFwd();

        /* Keep a copy of the registered L2CAP PSM, maybe useful later */
        theScoFwd->local_psm = cfm->psm;
        /* Copy and update SDP record */
        uint8 *record = PanicUnlessMalloc(appSdpGetScoFwdServiceRecordSize());
        memcpy(record, appSdpGetScoFwdServiceRecord(), appSdpGetScoFwdServiceRecordSize());

        /* Write L2CAP PSM into service record */
        appSdpSetScoFwdPsm(record, cfm->psm);

        /* Set supported attributes if MIC forwarding is enabled */
        appSdpSetScoFwdFeatures(record, SFWD_FEATURE_RING_FWD | (appConfigMicForwardingEnabled() ? SFWD_FEATURE_MIC_FWD : 0));

        /* Register service record */
        ConnectionRegisterServiceRecord(GetScoFwdTask(), appSdpGetScoFwdServiceRecordSize(), record);
    }
    else
    {
        DEBUG_LOG("ScoFwdHandleL2capRegisterCfm, failed to register L2CAP PSM");
        Panic();
    }
}

static void ScoFwdHandleClSdpRegisterCfm(const CL_SDP_REGISTER_CFM_T *cfm)
{
    DEBUG_LOG("ScoFwdHandleClSdpRegisterCfm, status %d", cfm->status);
    PanicFalse(ScoFwdGetState() == SFWD_STATE_INITIALISING);

    if (cfm->status == sds_status_success)
    {
        /* Move to 'idle' state */
        ScoFwdSetState(SFWD_STATE_IDLE);
    }
    else
        Panic();
}

static bool ScoFwdGetL2capPSM(const uint8 *begin, const uint8 *end, uint16 *psm, uint16 id)
{
    ServiceDataType type;
    Region record, protocols, protocol, value;
    record.begin = begin;
    record.end   = end;

    while (ServiceFindAttribute(&record, id, &type, &protocols))
        if (type == sdtSequence)
            while (ServiceGetValue(&protocols, &type, &protocol))
            if (type == sdtSequence
               && ServiceGetValue(&protocol, &type, &value)
               && type == sdtUUID
               && RegionMatchesUUID32(&value, (uint32)0x0100)
               && ServiceGetValue(&protocol, &type, &value)
               && type == sdtUnsignedInteger)
            {
                *psm = (uint16)RegionReadUnsigned(&value);
                return TRUE;
            }

    return FALSE;
}


static void ScoFwdHandleClSdpServiceSearchAttributeCfm(const CL_SDP_SERVICE_SEARCH_ATTRIBUTE_CFM_T *cfm)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    DEBUG_LOG("ScoFwdHandleClSdpServiceSearchAttributeCfm, status %d", cfm->status);

    switch (ScoFwdGetState())
    {
        case SFWD_STATE_SDP_SEARCH:
        {
            /* Find the PSM in the returned attributes */
            if (cfm->status == sdp_response_success)
            {
                if (ScoFwdGetL2capPSM(cfm->attributes, cfm->attributes + cfm->size_attributes,
                                         &theScoFwd->remote_psm, saProtocolDescriptorList))
                {
                    DEBUG_LOG("ScoFwdHandleClSdpServiceSearchAttributeCfm, peer psm %u", theScoFwd->remote_psm);

                    ScoFwdConnectL2cap();
                    ScoFwdSetState(SFWD_STATE_CONNECTING);
                }
                else
                {
                    /* No PSM found, malformed SDP record on peer? */
                    scoFwd_SendConnectClientsCfm(sfwd_status_connect_failed_sdp);
                    ScoFwdSetState(SFWD_STATE_IDLE);
                }
            }
            else if (cfm->status == sdp_no_response_data)
            {
                /* Peer Earbud doesn't support SCO forwarding service */
                scoFwd_SendConnectClientsCfm(sfwd_status_connect_failed_sdp);
                ScoFwdSetState(SFWD_STATE_IDLE);
            }
            else
            {
                /* SDP seach failed, retry? */
                if (theScoFwd->sdp_retry_count--)
                {
                    ScoFwdSetState(SFWD_STATE_SDP_SEARCH);
                }
                else
                {
                    /* reset retry count, return failure and back to idle */
                    theScoFwd->sdp_retry_count = ScoFwdConfigSdpSearchRetryCount();
                    scoFwd_SendConnectClientsCfm(sfwd_status_connect_failed_sdp);
                    ScoFwdSetState(SFWD_STATE_IDLE);
                }
            }
        }
        break;

        default:
        {
            /* Silently ignore, not the end of the world */
        }
        break;
    }
}

static void ScoFwdHandleL2capConnectInd(const CL_L2CAP_CONNECT_IND_T *ind)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    DEBUG_LOG("ScoFwdHandleL2capConnectInd, state %u, psm %u", ScoFwdGetState(), ind->psm);
    PanicFalse(ind->psm == theScoFwd->local_psm);
    bool accept = FALSE;

    static const uint16 l2cap_conftab[] =
    {
        /* Configuration Table must start with a separator. */
        L2CAP_AUTOPT_SEPARATOR,
        /* Local Flush Timeout  - Accept Non-default Timeout*/
        L2CAP_AUTOPT_FLUSH_OUT,
            BKV_UINT32R(SFWD_FLUSH_MIN_US,SFWD_FLUSH_MAX_US),
        L2CAP_AUTOPT_FLUSH_IN,
            BKV_UINT32R(SFWD_FLUSH_MIN_US,SFWD_FLUSH_MAX_US),
        L2CAP_AUTOPT_TERMINATOR
    };

    /* Only accept connection if it's from the peer and we're in the idle state */
    if (appDeviceIsPeer(&ind->bd_addr))
    {
        switch (ScoFwdGetState())
        {
            case SFWD_STATE_IDLE:
                accept = TRUE;
                DEBUG_LOG("ScoFwdHandleL2capConnectInd, idle, accept");
                ScoFwdSetState(SFWD_STATE_CONNECTING);
                break;

            case SFWD_STATE_CONNECTING:
                /* Connection crossover, accept the connection if we're the left Earbud */
                accept = Multidevice_IsLeft();
                DEBUG_LOG("ScoFwdHandleL2capConnectInd, crossover, accept %u", accept);
                break;

            default:
                /* Incorrect state, reject the connection */
                DEBUG_LOG("ScoFwdHandleL2capConnectInd, reject");
                accept = FALSE;
                break;
        }
    }

    /* Keep track of this connection */
    theScoFwd->pending_connects++;

    /* Send a response accepting or rejcting the connection. */
    ConnectionL2capConnectResponse(GetScoFwdTask(),     /* The client task. */
                                   accept,                 /* Accept/reject the connection. */
                                   ind->psm,               /* The local PSM. */
                                   ind->connection_id,     /* The L2CAP connection ID.*/
                                   ind->identifier,        /* The L2CAP signal identifier. */
                                   CONFTAB_LEN(l2cap_conftab),
                                   l2cap_conftab);          /* The configuration table. */
}

static void ScoFwdHandleL2capConnectCfm(const CL_L2CAP_CONNECT_CFM_T *cfm)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    DEBUG_LOG("ScoFwdHandleL2capConnectCfm, status %u, pending %u", cfm->status, theScoFwd->pending_connects);

    /* Pending connection, return, will get another message in a bit */
    if (l2cap_connect_pending == cfm->status)
    {
        DEBUG_LOG("ScoFwdHandleL2capConnectCfm, connect pending, wait");
        return;
    }

    /* Decrement number of pending connect confirms, panic if 0 */
    PanicFalse(theScoFwd->pending_connects > 0);
    theScoFwd->pending_connects--;

    switch (ScoFwdGetState())
    {
        case SFWD_STATE_CONNECTING:
            /* If connection was succesful, get sink, attempt to enable wallclock and move
             * to connected state */
            if (l2cap_connect_success == cfm->status)
            {
                DEBUG_LOG("ScoFwdHandleL2capConnectCfm, connected, conn ID %u, flush remote %u", cfm->connection_id, cfm->flush_timeout_remote);

                PanicNull(cfm->sink);
                theScoFwd->link_sink = cfm->sink;
                theScoFwd->link_source = StreamSourceFromSink(cfm->sink);
                scoFwd_SendConnectClientsCfm(sfwd_status_success);
                ScoFwdSetState(SFWD_STATE_CONNECTED);
            }
            else
            {
                /* Connection failed, if no more pending connections, return to idle state */
                if (theScoFwd->pending_connects == 0)
                {
                    DEBUG_LOG("ScoFwdHandleL2capConnectCfm, failed, go to idle state");
                    scoFwd_SendConnectClientsCfm(sfwd_status_connect_failed_l2cap);
                    ScoFwdSetState(SFWD_STATE_IDLE);
                }
                else
                {
                    DEBUG_LOG("ScoFwdHandleL2capConnectCfm, failed, wait");
                }
            }
            break;

        default:
            /* Connect confirm receive not in connecting state, connection must have failed */
            PanicFalse(l2cap_connect_success != cfm->status);
            DEBUG_LOG("ScoFwdHandleL2capConnectCfm, failed, pending %u", theScoFwd->pending_connects);
            break;
    }
}


static void ScoFwdHandleL2capDisconnectInd(const CL_L2CAP_DISCONNECT_IND_T *ind)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    DEBUG_LOG("ScoFwdHandleL2capDisconnectInd, status %u", ind->status);

    /* Always send reponse */
    ConnectionL2capDisconnectResponse(ind->identifier, ind->sink);

    /* Only change state if sink matches */
    if (ind->sink == theScoFwd->link_sink)
    {
        /* if we have any clients expecting a disconnect cfm, inform success,
         * the state is now what they requested */
        scoFwd_SendDisconnectClientsCfm(sfwd_status_success);
        ScoFwdSetState(SFWD_STATE_IDLE);
    }
}


static void ScoFwdHandleL2capDisconnectCfm(const CL_L2CAP_DISCONNECT_CFM_T *cfm)
{
    UNUSED(cfm);
    DEBUG_LOG("ScoFwdHandleL2capDisconnectCfm, status %u", cfm->status);

    /* Move to idle state if we're in the disconnecting state */
    if (ScoFwdGetState() == SFWD_STATE_DISCONNECTING)
    {
        scoFwd_SendDisconnectClientsCfm(sfwd_status_success);
        ScoFwdSetState(SFWD_STATE_IDLE);
    }
}


static void ScoFwdHandleMMD(const MessageMoreData *mmd)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();

    if (mmd->source == theScoFwd->forwarding_source)
    {

    }
    else if (mmd->source == theScoFwd->link_source)
    {
        ScoFwdProcessForwardedAudio();
    }
    else
    {
        DEBUG_LOG("MMD received that doesn't match a link");
        if (ALL_SOURCES_CONFIGURED(theScoFwd))
        {
            Panic();
        }
    }
}

/*! \brief Handle messages about the interface between the outside world
           (L2CAP) and the audio chain.

     We treat space in the target buffer in the same way as more data in the
     send buffer. May need to assess if we need this, but neccessary for
     making sure stall situations recover (if possible).
 */
static void ScoFwdHandleMMS(const MessageMoreSpace *mms)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();

    if (mms->sink == theScoFwd->link_sink)
    {
    }
    else if (mms->sink == theScoFwd->forwarded_sink)
    {
        ScoFwdProcessForwardedAudio();
    }
    else
    {
        DEBUG_LOG("MMS received that doesn't match a link");
        if (ALL_SINKS_CONFIGURED(theScoFwd))
        {
            Panic();
        }
    }
}

static void ScoFwdHandleStartReceiveChain(void)
{
    if (!ScoFwdIsStreaming() && ScoFwdIsConnected())
    {
        /* Move to receive state */
        ScoFwdSetState(SFWD_STATE_CONNECTED_ACTIVE_RECEIVE);
    }
    else
    {
        DEBUG_LOG("ScoFwdHandleStartChain Asked to start when already active");
    }
}

static void ScoFwdHandleStopReceiveChain(void)
{
    if (ScoFwdIsStreaming())
    {
        ScoFwdSetState(SFWD_STATE_CONNECTED);
    }
    else
    {
        DEBUG_LOG("ScoFwdHandleStopChain Asked to stop when not active. State %d",ScoFwdGetState());
    }
}

static void ScoFwdHandleKickProcessing(void)
{
    if (ScoFwdInActiveState())
    {
        ScoFwdProcessForwardedAudio();
    }
}

/*  We have stopped receiving packets, but the link etc. is still up.

    Stopping the late packet timer will mean we do no more work on the
    speaker path (which will have muted by now anyway). Clearing the ttp
    means that when audio restarts we just forward the first packet.
 */
static void ScoFwdHandleRxAudioMissing(void)
{
    scoFwdReceivedPacketStats *stats = GetScoFwdStats();

    /* Make sure audio still missing */
    if (stats->audio_missing)
    {
        cancel_late_packet_timer();
        clear_last_received_ttp();
    }
}

/*! Get forwarding started.
    Only works if we have an active SCO already on the master */
static void ScoFwdHandleEnableForwarding(void)
{
    DEBUG_LOG("ScoFwdHandleEnableForwarding SFWD state %u", ScoFwdGetState());

    if (ScoFwdGetState() == SFWD_STATE_CONNECTED_ACTIVE)
    {
        ScoFwdSetState(SFWD_STATE_CONNECTED_ACTIVE_SEND);
    }
}

/* Stop forwarding.
   Only works if we're currently a master forwarding SCO. */
static void ScoFwdHandleDisableForwarding(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();

    DEBUG_LOG("ScoFwdHandleDisableForwarding SFWD state %u", ScoFwdGetState());

    if (theScoFwd->state == SFWD_STATE_CONNECTED_ACTIVE_SEND)
    {
        /* Move to connected state */
        ScoFwdSetState(SFWD_STATE_CONNECTED_ACTIVE);
    }
}

static void ScoFwdHandlePeerSignallingMarshalledMessage(const PEER_SIG_MARSHALLED_MSG_CHANNEL_RX_IND_T *ind)
{
    DEBUG_LOG("ScoFwdHandlePeerSignallingMarshalledMessage. Channel 0x%x, type %d", ind->channel, ind->type);

    switch (ind->type)
    {
    case MARSHAL_TYPE_scofwd_profile_ota_message_ind_t:
        {
            const scofwd_profile_ota_message_ind_t *ota_ind = (const scofwd_profile_ota_message_ind_t*)ind->msg;
            ProcessOTAControlMessage(ota_ind->ota_msg_id);
        }
        break;

    case MARSHAL_TYPE_scofwd_profile_volume_ind_t:
        {
        }
        break;

    case MARSHAL_TYPE_scofwd_profile_volume_start_ind_t:
        {
        }
        break;

    case MARSHAL_TYPE_scofwd_profile_volume_stop_ind_t:
        {
        }
        break;

    default:
        break;
    }

    /* free unmarshalled msg */
    free(ind->msg);
}

static void ScoFwdHandlePeerSignallingMarshalledMessageTxCfm(const PEER_SIG_MARSHALLED_MSG_CHANNEL_TX_CFM_T *cfm)
{
    peerSigStatus status = cfm->status;

    if (peerSigStatusSuccess != status)
    {
        DEBUG_LOG("ScoFwdHandlePeerSignallingMarshalledMessageTxCfm reports failure code 0x%x(%d)", status, status);
    }
}

static void ScoFwdHandlePeerSignallingConnectionInd(const PEER_SIG_CONNECTION_IND_T *ind)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    DEBUG_LOG("ScoFwdHandlePeerSignallingConnectionInd, status %u", ind->status);

    /* Peer signalling has disconnected, therefore we don't know if peer has
     * incoming call or not */
    if (ind->status == peerSigStatusDisconnected)
        theScoFwd->peer_incoming_call = FALSE;
}

static void ScoFwdForwardVolume(bool enabled)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    theScoFwd->forward_volume = enabled;
}

static void ScoFwdHandleRoleNotify(SFWD_INTERNAL_ROLE_NOTIFY_T *role)
{
    DEBUG_LOG("ScoFwdHandleRoleNotify, status %u, role %u", ScoFwdGetState(), role->role);

    if (ScoFwdGetState() == SFWD_STATE_CONNECTED_ACTIVE_SEND_PENDING_ROLE_IND)
    {
        if (role->role == hci_role_master)
            ScoFwdSetState(SFWD_STATE_CONNECTED_ACTIVE_SEND);
    }
}

/*! \brief Message Handler

    This function is the main message handler for SCO forwarding, every
    message is handled in it's own seperate handler function.

    The different groups of messages are separated in the switch statement
    by a comment like this ----
*/
static void ScoFwdHandleMessage(Task task, MessageId id, Message message)
{
    UNUSED(task);

#ifdef SCOFWD_EXTRA_DEBUG
    if (   id != MESSAGE_MORE_DATA
        && id != MESSAGE_MORE_SPACE)
    {
        DEBUG_LOG("**** ScoFwdHandleMessage: MESSAGE:ScoFwdInternalMessageId_t:0x%x", id);
    }
#endif

    switch (id)
    {
        case PEER_SIG_MARSHALLED_MSG_CHANNEL_RX_IND:
            ScoFwdHandlePeerSignallingMarshalledMessage((const PEER_SIG_MARSHALLED_MSG_CHANNEL_RX_IND_T *)message);
            break;

        case PEER_SIG_MARSHALLED_MSG_CHANNEL_TX_CFM:
            ScoFwdHandlePeerSignallingMarshalledMessageTxCfm((const PEER_SIG_MARSHALLED_MSG_CHANNEL_TX_CFM_T *)message);
            break;

        case PEER_SIG_CONNECTION_IND:
            ScoFwdHandlePeerSignallingConnectionInd((const PEER_SIG_CONNECTION_IND_T *)message);
            break;

        /*----*/

        case CL_L2CAP_REGISTER_CFM:
            ScoFwdHandleL2capRegisterCfm((const CL_L2CAP_REGISTER_CFM_T *)message);
            break;

        case CL_SDP_REGISTER_CFM:
            ScoFwdHandleClSdpRegisterCfm((const CL_SDP_REGISTER_CFM_T *)message);
            break;

        case CL_SDP_SERVICE_SEARCH_ATTRIBUTE_CFM:
            ScoFwdHandleClSdpServiceSearchAttributeCfm((const CL_SDP_SERVICE_SEARCH_ATTRIBUTE_CFM_T *)message);
            return;

        case CL_L2CAP_CONNECT_IND:
            ScoFwdHandleL2capConnectInd((const CL_L2CAP_CONNECT_IND_T *)message);
            break;

        case CL_L2CAP_CONNECT_CFM:
            ScoFwdHandleL2capConnectCfm((const CL_L2CAP_CONNECT_CFM_T *)message);
            break;

        case CL_L2CAP_DISCONNECT_IND:
            ScoFwdHandleL2capDisconnectInd((const CL_L2CAP_DISCONNECT_IND_T *)message);
            break;

        case CL_L2CAP_DISCONNECT_CFM:
            ScoFwdHandleL2capDisconnectCfm((const CL_L2CAP_DISCONNECT_CFM_T *)message);
            break;

        /*----*/

        case MESSAGE_MORE_DATA:
            ScoFwdHandleMMD((const MessageMoreData*)message);
            break;

        case MESSAGE_MORE_SPACE:
            ScoFwdHandleMMS((const MessageMoreSpace*)message);
            break;

        case MESSAGE_SOURCE_EMPTY:
            break;

        /*----*/

        case SFWD_INTERNAL_LINK_CONNECT_REQ:
            ScoFwdHandleLinkConnectReq();
            break;

        case SFWD_INTERNAL_LINK_DISCONNECT_REQ:
            ScoFwdHandleLinkDisconnectReq();
            break;

        case SFWD_INTERNAL_START_RX_CHAIN:
            ScoFwdHandleStartReceiveChain();
            break;

        case SFWD_INTERNAL_STOP_RX_CHAIN:
            ScoFwdHandleStopReceiveChain();
            break;

        case SFWD_INTERNAL_KICK_PROCESSING:
            ScoFwdHandleKickProcessing();
            break;

        case SFWD_INTERNAL_RX_AUDIO_MISSING:
            ScoFwdHandleRxAudioMissing();
            break;

        case SFWD_INTERNAL_ENABLE_FORWARDING:
            ScoFwdHandleEnableForwarding();
            break;

        case SFWD_INTERNAL_DISABLE_FORWARDING:
            ScoFwdHandleDisableForwarding();
            break;

        case SFWD_INTERNAL_ROLE_NOTIFY:
            ScoFwdHandleRoleNotify((SFWD_INTERNAL_ROLE_NOTIFY_T *)message);
            break;

        case SFWD_INTERNAL_PLAY_RING:
            TaskList_MessageSendId(TaskList_GetFlexibleBaseTaskList(ScoFwdGetClients()), SCOFWD_RINGING);
            break;

        /*----*/

        case SFWD_INTERNAL_MIC_CHAIN_DETAILS:
            return;

        /*----*/

        case SFWD_TIMER_LATE_PACKET:
            break;

        /*----*/

        default:
            DEBUG_LOG("ScoFwdHandleMessage. UNHANDLED Message id MESSAGE:ScoFwdInternalMessageId_t:0x%x. State %d", id, ScoFwdGetState());
            break;
    }
}

bool ScoFwdIsStreaming(void)
{
    return ScoFwdInActiveState();
}

bool ScoFwdIsReceiving(void)
{
    return (ScoFwdGetState() == SFWD_STATE_CONNECTED_ACTIVE_RECEIVE);
}

bool ScoFwdIsSending(void)
{
    return (ScoFwdGetState() == SFWD_STATE_CONNECTED_ACTIVE_SEND);
}

bool ScoFwdIsConnected(void)
{
    return (ScoFwdGetState() == SFWD_STATE_CONNECTED ||
            ScoFwdGetState() == SFWD_STATE_CONNECTED_ACTIVE_SEND ||
            ScoFwdGetState() == SFWD_STATE_CONNECTED_ACTIVE_SEND_PENDING_ROLE_IND ||
            ScoFwdGetState() == SFWD_STATE_CONNECTED_ACTIVE_RECEIVE);
}

bool ScoFwdIsDisconnected(void)
{
    return (ScoFwdGetState() == SFWD_STATE_IDLE);
}

/*! \brief Initialise SCO Forwarding task

    Called at start up to initialise the SCO forwarding task
*/
bool ScoFwdInit(Task init_task)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();

    UNUSED(init_task);

    /* Set up task handler */
    theScoFwd->task.handler = ScoFwdHandleMessage;

    /* initialise tasklists */
    TaskList_InitialiseWithCapacity(ScoFwdGetClients(), THE_SCOFWD_CLIENT_TASK_LIST_INIT_CAPACITY);
    TaskList_InitialiseWithCapacity(ScoFwdGetConnectReqTasks(), THE_SCOFWD_CONNECT_REQ_TASK_LIST_INIT_CAPACITY);
    TaskList_InitialiseWithCapacity(ScoFwdGetDisonnectReqTasks(), THE_SCOFWD_DISCONNECT_REQ_CLIENT_TASK_LIST_INIT_CAPACITY);

    /* Register a Protocol/Service Multiplexor (PSM) that will be
       used for this application. The same PSM is used at both
       ends. */
    ConnectionL2capRegisterRequest(GetScoFwdTask(), L2CA_PSM_INVALID, 0);

    /* Initialise state */
    theScoFwd->state = SFWD_STATE_NULL;
    ScoFwdSetState(SFWD_STATE_INITIALISING);

    /* Default to forwarding HFP volume changes to Secondary. */
    ScoFwdForwardVolume(TRUE);

    /* Register a channel for peer signalling */
    appPeerSigMarshalledMsgChannelTaskRegister(GetScoFwdTask(),
        PEER_SIG_MSG_CHANNEL_SCOFWD,
        scofwd_profile_marshal_type_descriptors,
        NUMBER_OF_SCOFWD_PROFILE_MARSHAL_TYPES);

    /* Register for peer signaling notifications */
    appPeerSigClientRegister(GetScoFwdTask());

    ScoFwdSetWallclock((Sink)NULL);

    Ui_RegisterUiProvider(ui_provider_scofwd, ScoFwd_GetCurrentContext);

    theScoFwd->sdp_retry_count = ScoFwdConfigSdpSearchRetryCount();

    return TRUE;
}

/* Set-up a link between the devices for forwarding SCO audio. */
void ScoFwdConnectPeer(Task client)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();

    DEBUG_LOG("ScoFwdConnectPeer");

    /* if any clients were waiting on disconnection, then inform that has
     * been cancelled now that we're connecting. */
    if (MessageCancelAll(GetScoFwdTask(), SFWD_INTERNAL_LINK_DISCONNECT_REQ))
    {
        scoFwd_SendDisconnectClientsCfm(sfwd_status_disconnect_cancelled);
    }

    /* add client to connect list */
    TaskList_AddTask(TaskList_GetFlexibleBaseTaskList(ScoFwdGetConnectReqTasks()), client);

    MessageSendConditionally(GetScoFwdTask(), SFWD_INTERNAL_LINK_CONNECT_REQ, NULL, &theScoFwd->lock);
}

/*! \brief Inform the Peer earbud that we're done with HFP */
void ScoFwdDisconnectPeer(Task client)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();

    DEBUG_LOG("ScoFwdDisconnectPeer");

    if (MessageCancelAll(GetScoFwdTask(), SFWD_INTERNAL_LINK_CONNECT_REQ))
    {
        scoFwd_SendConnectClientsCfm(sfwd_status_connect_cancelled);
    }

    TaskList_AddTask(TaskList_GetFlexibleBaseTaskList(ScoFwdGetDisonnectReqTasks()), client);

    MessageSendConditionally(GetScoFwdTask(), SFWD_INTERNAL_LINK_DISCONNECT_REQ, NULL, &theScoFwd->lock);
}

bool ScoFwdIsCallIncoming(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();

    DEBUG_LOG("ScoFwdIsCallIncoming: Checking for incoming call (%d)",theScoFwd->peer_incoming_call);

    return theScoFwd->peer_incoming_call;
}

void ScoFwdCallAccept(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    DEBUG_LOG("ScoFwdCallAccept");

    SendOTAControlMessage(SFWD_OTA_MSG_CALL_ANSWER);
    theScoFwd->peer_incoming_call = FALSE;
}

void ScoFwdCallReject(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    DEBUG_LOG("ScoFwdCallReject");

    SendOTAControlMessage(SFWD_OTA_MSG_CALL_REJECT);
    theScoFwd->peer_incoming_call = FALSE;
}

void ScoFwdCallHangup(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    DEBUG_LOG("ScoFwdCallHangup");

    SendOTAControlMessage(SFWD_OTA_MSG_CALL_HANGUP);
    theScoFwd->peer_incoming_call = FALSE;
}

void ScoFwdCallVoice(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    DEBUG_LOG("ScoFwdCallVoice");

    SendOTAControlMessage(SFWD_OTA_MSG_CALL_VOICE);
    theScoFwd->peer_incoming_call = FALSE;
}

void ScoFwdVolumeStart(int16 step)
{
    scofwd_profile_volume_start_ind_t *ind = PanicUnlessMalloc(sizeof(*ind));
    ind->steps = step;
    appPeerSigMarshalledMsgChannelTx(GetScoFwdTask(),
                                    PEER_SIG_MSG_CHANNEL_SCOFWD,
                                    ind, MARSHAL_TYPE(scofwd_profile_volume_start_ind_t));
}

void ScoFwdVolumeStop(int16 step)
{
    scofwd_profile_volume_stop_ind_t *ind = PanicUnlessMalloc(sizeof(*ind));
    ind->steps = step;
    appPeerSigMarshalledMsgChannelTx(GetScoFwdTask(),
                                    PEER_SIG_MSG_CHANNEL_SCOFWD,
                                    ind, MARSHAL_TYPE(scofwd_profile_volume_stop_ind_t));
}

void ScoFwdEnableForwarding(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    MessageSendConditionally(GetScoFwdTask(), SFWD_INTERNAL_ENABLE_FORWARDING, NULL, &theScoFwd->lock);
}

void ScoFwdDisableForwarding(void)
{
    scoFwdTaskData *theScoFwd = GetScoFwd();
    MessageSendConditionally(GetScoFwdTask(), SFWD_INTERNAL_DISABLE_FORWARDING, NULL, &theScoFwd->lock);
}

/*! \brief Set the time base for the SCOFWD to match a sink

    Stores the supplied sink for the RING synchronization.

    \param sink The sink to be used as a time base.
*/
void ScoFwdSetWallclock(Sink sink)
{
    DEBUG_LOG("ScoFwdSetWallclock");
    scoFwdTaskData *theScoFwd = GetScoFwd();
    theScoFwd->wallclock_sink = sink;
}

bool ScoFwdProfile_IsScoForwardingAllowed(void)
{
    return FALSE;
}

bool ScoFwdProfile_IsMicForwardingAllowed(void)
{
    return (ScoFwdProfile_IsScoForwardingAllowed() && scoFwdProfile_IsPeerMicForwardSupported());
}


static unsigned ScoFwd_GetCurrentContext(void)
{
    sco_fwd_provider_context_t context = BAD_CONTEXT;

    switch (ScoFwdGetState())
    {
    case SFWD_STATE_NULL:
    case SFWD_STATE_INITIALISING:
    case SFWD_STATE_IDLE:
    case SFWD_STATE_SDP_SEARCH:
    case SFWD_STATE_CONNECTING:
        break;
    case SFWD_STATE_CONNECTED:
        if (ScoFwdIsCallIncoming())
        {
            context = context_sco_fwd_call_incoming;
        }
        else
        {
            context = context_sco_fwd_connected;
        }
        break;
    case SFWD_STATE_CONNECTED_ACTIVE:
    case SFWD_STATE_CONNECTED_ACTIVE_RECEIVE:
    case SFWD_STATE_CONNECTED_ACTIVE_SEND:
        if (ScoFwdIsCallIncoming())
        {
            context = context_sco_fwd_call_incoming;
        }
        else
        {
            context = context_sco_fwd_call_ongoing;
        }
        break;
    case SFWD_STATE_DISCONNECTING:
        break;
    default:
        break;
    }
    return (unsigned)context;
}

static void scoFwdProfile_RegisterMessageGroup(Task task, message_group_t group)
{
    PanicFalse(group == SFWD_MESSAGE_GROUP);
#ifdef INCLUDE_SCOFWD
    TaskList_AddTask(TaskList_GetFlexibleBaseTaskList(ScoFwdGetClients()), task);
#else
    DEBUG_LOG("scoFwdProfile_RegisterMessageGroup(%p) SCOFWD disabled", task);
#endif
}

MESSAGE_BROKER_GROUP_REGISTRATION_MAKE(SFWD, scoFwdProfile_RegisterMessageGroup, NULL);
