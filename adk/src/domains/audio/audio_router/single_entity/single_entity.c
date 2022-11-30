/*!
\copyright  Copyright (c) 2020-2021 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\file
\defgroup   audio_router single_entity
\ingroup    audio_domain
\brief      Implementation of audio router functions for single signal path.
*/

#include "single_entity.h"
#include "audio_router.h"
#include "focus_audio_source.h"
#include "focus_voice_source.h"
#include "logging.h"
#include "single_entity_data.h"
#include "device.h"
#include "device_properties.h"
#include "av.h"
#include "panic.h"
#include "stdlib.h"

static void singleEntity_AddSource(generic_source_t source);
static void singleEntity_RemoveSource(generic_source_t source);
static bool singleEntity_IsDeviceInUse(device_t device);
static void singleEntity_Update(void);

static const audio_router_t single_entity_router_functions =
{
    .add_source = singleEntity_AddSource,
    .remove_source = singleEntity_RemoveSource,
    .is_device_in_use = singleEntity_IsDeviceInUse,
    .update = singleEntity_Update
};

bool SingleEntity_Init(Task init_task)
{
    UNUSED(init_task);
    AudioRouter_ConfigureHandlers(SingleEntity_GetHandlers());
    AudioRouter_InitData();
    return TRUE;
}

const audio_router_t* SingleEntity_GetHandlers(void)
{
    DEBUG_LOG_FN_ENTRY("SingleEntity_GetHandlers");

    return &single_entity_router_functions;
}

static source_status_t singleEntity_StateConnectingAction(generic_source_t source)
{
    source_status_t status = AudioRouter_CommonSetSourceState(source, source_state_connecting);

    DEBUG_LOG_FN_ENTRY("singleEntity_StateConnectingAction response enum:source_status_t:%d", status);

    if(status == source_status_ready)
    {
        if(!SingleEntityData_IsSourcePresent(source))
        {   /* don't continue connecting if source has gone */
            /* start disconnecting */
            SingleEntityData_SetSourceState(source, audio_router_state_disconnecting_no_connect);
        }
        else
        {
            if(!AudioRouter_CommonConnectSource(source))
            {
                DEBUG_LOG_INFO("singleEntity_StateConnectingAction unable to connect audio");
            }
            SingleEntityData_SetSourceState(source, audio_router_state_connected_pending);
        }
    }
    return status;
}

static source_status_t singleEntity_StateConnectedPendingAction(generic_source_t source)
{
    source_status_t status = AudioRouter_CommonSetSourceState(source, source_state_connected);

    DEBUG_LOG_FN_ENTRY("singleEntity_StateConnectedPendingAction response enum:source_status_t:%d", status);

    if(status != source_status_ready)
    {
        DEBUG_LOG_INFO("singleEntity_StateConnectingAction bad response from setting source connected");
        Panic();
    }

    SingleEntityData_SetSourceState(source, audio_router_state_connected);

    return status;
}

static source_status_t singleEntity_StateDisconnectingAction(generic_source_t source)
{
    source_status_t status = AudioRouter_CommonSetSourceState(source, source_state_disconnecting);

    DEBUG_LOG_FN_ENTRY("singleEntity_StateDisconnectingAction response enum:source_status_t:%d", status);

    if(status == source_status_ready)
    {
        if(AudioRouter_CommonDisconnectSource(source))
        {
            SingleEntityData_SetSourceState(source, audio_router_state_disconnected_pending);
        }
        else
        {
            DEBUG_LOG_INFO("singleEntity_StateDisconnectingAction unable to disconnect audio");
            Panic();
        }
    }
    return status;
}

static source_status_t singleEntity_StateDisconnectingNoConnectAction(generic_source_t source)
{
    source_status_t status = AudioRouter_CommonSetSourceState(source, source_state_disconnecting);

    DEBUG_LOG_FN_ENTRY("singleEntity_StateDisconnectingNoConnectAction response enum:source_status_t:%d", status);

    if(status == source_status_ready)
    {
        SingleEntityData_SetSourceState(source, audio_router_state_disconnected_pending);
    }

    return status;
}

static source_status_t singleEntity_StateDisconnectedPendingAction(generic_source_t source)
{
    source_status_t status = AudioRouter_CommonSetSourceState(source, source_state_disconnected);

    DEBUG_LOG_FN_ENTRY("singleEntity_StateDisconnectedPendingAction response enum:source_status_t:%d", status);

    if(status == source_status_ready)
    {
        SingleEntityData_SetSourceState(source, audio_router_state_disconnected);
    }

    return status;
}

static bool singleEntity_AttemptStableState(generic_source_t source)
{
#define MAX_LOOP 8

    bool stable = FALSE;
    unsigned iterations = MAX_LOOP;
    source_status_t response = source_status_error;

    DEBUG_LOG_FN_ENTRY("singleEntity_AttemptStableState source enum:source_type_t:%d, source=%d",
                        source.type, source.u.audio);

    while(iterations-- && !stable)
    {
        switch(SingleEntityData_GetSourceState(source))
        {
            case audio_router_state_connected:
            case audio_router_state_disconnected:
            case audio_router_state_invalid:
                stable = TRUE;
                break;

            case audio_router_state_connected_pending:
                response = singleEntity_StateConnectedPendingAction(source);
                break;

            case audio_router_state_connecting:
                response = singleEntity_StateConnectingAction(source);
                break;

            case audio_router_state_disconnecting:
                response = singleEntity_StateDisconnectingAction(source);
                break;

            case audio_router_state_disconnecting_no_connect:
                response = singleEntity_StateDisconnectingNoConnectAction(source);
                break;

            case audio_router_state_disconnected_pending:
                response = singleEntity_StateDisconnectedPendingAction(source);
                break;

            default:
                Panic();
                break;
        }

        if(!stable)
        {
            if(response == source_status_preparing)
            {
                break;
            }
            /* If response is anything other than ready at this point we have
               encountered an error */
            PanicFalse(response == source_status_ready);
        }
    }
    /* If we exited the while loop because the counter hit zero something has gone wrong */
    if(!iterations)
    {
        DEBUG_LOG_INFO("singleEntity_AttemptStableState failed to reach stable state");
        Panic();
    }

    return stable;
}

static bool singleEntity_RetryIfIntermediate(void)
{
    generic_source_t source = {0};

    DEBUG_LOG_FN_ENTRY("singleEntity_RetryIfIntermediate");

    if(SingleEntityData_FindTransientSource(&source))
    {
        return singleEntity_AttemptStableState(source);
    }

    return TRUE;
}

static bool singleEntity_RefreshRoutedSource(void)
{
    generic_source_t routed_source = {0};
    generic_source_t source_to_route = {0};

    bool have_source_to_route = SingleEntityData_GetSourceToRoute(&source_to_route);
    bool have_routed_source = SingleEntityData_GetActiveSource(&routed_source);

    bool disconnect = FALSE;
    bool connect = FALSE;
    bool stable = TRUE;

    DEBUG_LOG_FN_ENTRY("singleEntity_RefreshRoutedSource");

    /* Decide if connections or disconnections are required */
    if((!have_routed_source) && have_source_to_route)
    {
        connect = TRUE;
    }
    else if(have_routed_source && have_source_to_route)
    {
        if(!SingleEntityData_AreSourcesSame(source_to_route, routed_source))
        {
            connect = TRUE;
            disconnect = TRUE;
        }
    }
    else if(have_routed_source && (!have_source_to_route))
    {
        disconnect = TRUE;
    }

    /* Connect and disconnect as appropriate */
    if(disconnect)
    {
        DEBUG_LOG_VERBOSE("singleEntity_RefreshRoutedSource disconnecting enum:source_type_t:%d, source=%d",
                          routed_source.type, routed_source.u.audio);

        if(SingleEntityData_GetSourceState(routed_source) != audio_router_state_connected)
        {
            DEBUG_LOG_INFO("singleEntity_RefreshRoutedSource cannot disconnect a source that isn't connected");
            Panic();
        }

        SingleEntityData_SetSourceState(routed_source, audio_router_state_disconnecting);

        if(!singleEntity_AttemptStableState(routed_source))
        {/* didn't fully disconnect so cannot connect */
            connect = FALSE;
            stable = FALSE;
        }
    }

    if(connect)
    {
        audio_router_state_t source_to_route_state = SingleEntityData_GetSourceState(source_to_route);

        DEBUG_LOG_VERBOSE("singleEntity_RefreshRoutedSource Connecting enum:source_type_t:%d, source=%d",
                          source_to_route.type, source_to_route.u.audio);

        if((source_to_route_state != audio_router_state_disconnected) && (source_to_route_state != audio_router_state_new_source))
        {
            DEBUG_LOG_INFO("singleEntity_RefreshRoutedSource cannot connect a source that isn't disconnected");
            Panic();
        }

        SingleEntityData_SetSourceState(source_to_route, audio_router_state_connecting);

        stable = singleEntity_AttemptStableState(source_to_route);
    }

    return stable;
}


static void singleEntity_Update(void)
{
    if(singleEntity_RetryIfIntermediate())
    {
        if(singleEntity_RefreshRoutedSource())
        {
            generic_source_t source = {0};
            /* Notify new sources that won't be routed */
            while(SingleEntityData_FindNewSource(&source))
            {
                source_status_t status = AudioRouter_CommonSetSourceState(source, source_state_disconnected);
                if(status != source_status_ready)
                {
                    /* debug message */
                    Panic();
                }
                SingleEntityData_SetSourceState(source, audio_router_state_disconnected);
            }
        }
    }
}

static void singleEntity_AddSource(generic_source_t source)
{
    DEBUG_LOG_FN_ENTRY("singleEntity_AddSource enum:source_type_t:%d, source=%d", source.type, (unsigned)source.u.audio);

    if(SingleEntityData_AddSource(source))
    {
        singleEntity_Update();
    }
}

static void singleEntity_RemoveSource(generic_source_t source)
{
    DEBUG_LOG_FN_ENTRY("singleEntity_RemoveSource enum:source_type_t:%d, source=%d", source.type, (unsigned)source.u.audio);

    if(SingleEntityData_RemoveSource(source))
    {
        singleEntity_Update();
    }
}

static bool singleEntity_GetAudioSourceForDevice(device_t device, generic_source_t* source)
{
    audio_source_t audio_source;

    DEBUG_LOG_FN_ENTRY("singleEntity_GetAudioSourceForDevice");

    audio_source = DeviceProperties_GetAudioSource(device);

    if(audio_source != audio_source_none)
    {
        source->type = source_type_audio;
        source->u.audio = audio_source;
        return TRUE;
    }
    return FALSE;
}

static bool singleEntity_GetVoiceSourceForDevice(device_t device, generic_source_t* source)
{
    /* TODO do this once there's the notion of multiple HFP sources */
    UNUSED(device);
    UNUSED(source);

    DEBUG_LOG_FN_ENTRY("singleEntity_GetVoiceSourceForDevice");

    return FALSE;
}

static bool singleEntity_IsDeviceInUse(device_t device)
{
    generic_source_t source = {0};

    bool in_use = FALSE;

    DEBUG_LOG_FN_ENTRY("singleEntity_IsDeviceInUse");

    if(singleEntity_GetAudioSourceForDevice(device, &source))
    {
        if(SingleEntityData_IsSourceActive(source))
        {
            in_use = TRUE;
        }
    }

    if(!in_use && singleEntity_GetVoiceSourceForDevice(device, &source))
    {
        if(SingleEntityData_IsSourceActive(source))
        {
            in_use = TRUE;
        }
    }

    if(in_use)
    {
        DEBUG_LOG_VERBOSE("singleEntity_IsDeviceInUse enum:source_type_t:%d, source=%d", source.type, source.u.audio);
    }

    return in_use;
}
