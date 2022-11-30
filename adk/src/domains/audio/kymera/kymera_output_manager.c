/*!
\copyright  Copyright (c) 2020 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\brief      Kymera manager of the output chain
*/

#include "kymera_output_if.h"
#include "kymera_output_private.h"
#include "kymera_output_fixed_chain_config.h"
#include "kymera_private.h"
#include "kymera.h"

#define kymera_Register(registry, user_info) \
    do { \
    registry.entries = PanicNull(realloc(registry.entries, (registry.length + 1) * sizeof(*registry.entries))); \
    registry.entries[registry.length++] = user_info; \
    } while(0)

typedef const output_registry_entry_t* registry_entry_t;

typedef struct
{
    uint8 length;
    registry_entry_t *entries;
} registry_t;

typedef enum
{
    OutputConnectingIndication,
    OutputDisconnectedIndication,
} output_indications_t;

typedef const output_indications_registry_entry_t* indications_registry_entry_t;

typedef struct
{
    uint8 length;
    indications_registry_entry_t *entries;
} indications_registry_t;

static struct
{
    registry_t registry;
    indications_registry_t indications_registry;
    output_users_t ready_users;
    output_users_t connected_users;
    output_users_t custom_chain_creator;
    kymera_output_chain_config current_chain_config;
} state =
{
    .registry = {0, NULL},
    .ready_users = output_user_none,
    .connected_users = output_user_none,
    .custom_chain_creator = output_user_none,
    .current_chain_config = {0},
};

static registry_entry_t kymera_GetUserRegistryEntry(output_users_t user)
{
    for(unsigned i = 0; i < state.registry.length; i++)
    {
        if (state.registry.entries[i]->user == user)
            return state.registry.entries[i];
    }

    return NULL;
}

static inline registry_entry_t kymera_AssertValidUserRegistryEntry(output_users_t user)
{
    return PanicNull((void *)kymera_GetUserRegistryEntry(user));
}

static inline const output_custom_chain_creator_t * kymera_GetCustomChainCreatorInfo(output_users_t user)
{
    return PanicNull((void*)kymera_AssertValidUserRegistryEntry(user)->creator);
}

static inline output_connection_t kymera_GetUserConnectionType(output_users_t user)
{
    return kymera_AssertValidUserRegistryEntry(user)->connection;
}

static bool kymera_IsUserAssumedChainCompatible(output_users_t user)
{
#ifdef INCLUDE_FIXED_OUTPUT_CHAIN
    UNUSED(user);
    return TRUE;
#else
    return kymera_AssertValidUserRegistryEntry(user)->assume_chain_compatibility;
#endif
}

static inline bool kymera_IsRegisteredUser(output_users_t user)
{
    return (kymera_GetUserRegistryEntry(user) != NULL);
}

static inline bool kymera_IsCustomChainUser(output_users_t user)
{
    registry_entry_t entry = kymera_GetUserRegistryEntry(user);
    return (entry && entry->creator);
}

static void kymera_RegisterForIndications(const indications_registry_entry_t user_info)
{
    kymera_Register(state.indications_registry, user_info);
}

static void kymera_RegisterUser(const output_registry_entry_t *user_info)
{
    kymera_Register(state.registry, user_info);
}

static bool kymera_IsMainConnection(output_connection_t connection)
{
    switch(connection)
    {
        case output_connection_mono:
        case output_connection_stereo:
            return TRUE;
        default:
            return FALSE;
    }
}

static bool kymera_IsAuxConnection(output_connection_t connection)
{
    return (connection == output_connection_aux);
}

static bool kymera_CanConnectConcurrently(output_connection_t a, output_connection_t b)
{
    if (kymera_IsMainConnection(a) && kymera_IsMainConnection(b))
        return FALSE;

    if (kymera_IsAuxConnection(a) && kymera_IsAuxConnection(b))
        return FALSE;

    return TRUE;
}

static void kymera_UpdateInputSampleRate(output_users_t user, const kymera_output_chain_config *user_config)
{
    output_connection_t connection = kymera_GetUserConnectionType(user);

    if (kymera_IsMainConnection(connection))
        KymeraOutput_SetMainSampleRate(user_config->rate);

    if (kymera_IsAuxConnection(connection))
        KymeraOutput_SetAuxSampleRate(user_config->rate);
}

static inline bool kymera_IsStereoChainUser(output_users_t user)
{
    output_connection_t connection_type = kymera_GetUserConnectionType(user);
    return (connection_type != output_connection_mono) && appConfigOutputIsStereo();
}

static void kymera_OutputSendIndication(output_users_t user, output_indications_t indication)
{
    output_connection_t connection_type = kymera_IsRegisteredUser(user) ? kymera_GetUserConnectionType(user) : output_connection_none;

    DEBUG_LOG("kymera_OutputSendIndication: enum:output_indications_t:%d, triggered by: enum:output_users_t:%d, enum:output_connection_t:%d",
              indication, user, connection_type);

    const indications_registry_entry_t *entries = state.indications_registry.entries;
    for(unsigned i = 0; i < state.indications_registry.length; i++)
    {
        PanicNull((void *)entries[i]);
        switch(indication)
        {
            case OutputConnectingIndication:
                if (entries[i]->OutputConnectingIndication)
                    entries[i]->OutputConnectingIndication(user, connection_type);
                break;

            case OutputDisconnectedIndication:
                if (entries[i]->OutputDisconnectedIndication)
                    entries[i]->OutputDisconnectedIndication(user, connection_type);
                break;

            default:
                Panic();
        }
    }
}

static void kymera_CreateOutputChain(output_users_t user, const kymera_output_chain_config *user_config)
{
    DEBUG_LOG("kymera_CreateOutputChain");
#ifdef INCLUDE_FIXED_OUTPUT_CHAIN
    UNUSED(user_config);
    DEBUG_LOG("kymera_CreateOutputChain: Override user_config with fixed_config");
    state.current_chain_config = *Kymera_GetFixedOutputChainConfig();
#else
    state.current_chain_config = *user_config;
#endif
    KymeraOutput_CreateOperators(&state.current_chain_config, kymera_IsStereoChainUser(user));
    KymeraOutput_ConnectChain();
    appKymeraExternalAmpControl(TRUE);
}

static void kymera_DestroyOutputChain(void)
{
    DEBUG_LOG("kymera_DestroyOutputChain");
    memset(&state.current_chain_config, 0, sizeof(state.current_chain_config));
    appKymeraExternalAmpControl(FALSE);
    KymeraOutput_DestroyChain();
}

static bool kymera_ConnectToOutputChain(output_users_t user, const output_source_t *sources)
{
    bool status = TRUE;
    output_connection_t connection = kymera_GetUserConnectionType(user);

    kymera_OutputSendIndication(user, OutputConnectingIndication);

    if (state.custom_chain_creator)
        status = kymera_GetCustomChainCreatorInfo(state.custom_chain_creator)->OutputConnect(connection, sources);
    else
    {
        switch(connection)
        {
            case output_connection_mono:
                KymeraOutput_ConnectToMonoMainInput(sources->mono);
                break;
            case output_connection_aux:
                KymeraOutput_ConnectToAuxInput(sources->aux);
                break;
            case output_connection_stereo:
                KymeraOutput_ConnectToStereoMainInput(sources->stereo.left, sources->stereo.right);
                break;
            default:
                Panic();
                break;
        }
    }

    return status;
}

static void kymera_DisconnectFromOutputChain(output_users_t user)
{
    output_connection_t connection = kymera_GetUserConnectionType(user);

    if (state.custom_chain_creator)
        kymera_GetCustomChainCreatorInfo(state.custom_chain_creator)->OutputDisconnect(connection);
    else
    {
        switch(connection)
        {
            case output_connection_mono:
                KymeraOutput_DisconnectMonoMainInput();
                break;
            case output_connection_aux:
                KymeraOutput_DisconnectAuxInput();
                break;
            case output_connection_stereo:
                KymeraOutput_DisconnectStereoMainInput();
                break;
            default:
                Panic();
                break;
        }
    }

    kymera_OutputSendIndication(user, OutputDisconnectedIndication);
}

static bool kymera_IsCurrentChainCompatible(output_users_t user, const kymera_output_chain_config *config)
{
    if (kymera_IsUserAssumedChainCompatible(user))
        return TRUE;

    return (memcmp(config, &state.current_chain_config, sizeof(*config)) == 0);
}

static bool kymera_IsConnectedUser(output_users_t user)
{
    return (state.connected_users & user) != output_user_none;
}

static bool kymera_IsCurrentCreator(output_users_t user)
{
    // Can only have one custom chain creator
    return state.custom_chain_creator == user;
}

static output_users_t kymera_GetCurrentUsers(void)
{
    return state.ready_users | state.connected_users | state.custom_chain_creator;
}

static bool kymera_IsCurrentUser(output_users_t user)
{
    return (kymera_GetCurrentUsers() & user) != output_user_none;
}

static bool kymera_NoCurrentUsers(void)
{
    return kymera_GetCurrentUsers() == output_user_none;
}

static bool kymera_IsInputConnectable(output_connection_t input)
{
    const registry_entry_t *entries = state.registry.entries;
    // If there are no users the chain hasn't been created yet
    if (kymera_NoCurrentUsers() || (input == output_connection_none))
        return FALSE;

    for(unsigned i = 0; i < state.registry.length; i++)
    {
        if (kymera_IsConnectedUser(entries[i]->user))
        {
            if (kymera_CanConnectConcurrently(entries[i]->connection, input) == FALSE)
                return FALSE;
        }
    }

    return TRUE;
}

static bool kymera_IsUserConnectable(output_users_t user)
{
    return kymera_IsRegisteredUser(user) && kymera_IsInputConnectable(kymera_GetUserConnectionType(user));
}

static bool kymera_DisconnectUsers(output_users_t users)
{
    const registry_entry_t *entries = state.registry.entries;
    bool status = TRUE;

    for(unsigned i = 0; i < state.registry.length; i++)
    {
        if ((entries[i]->user & users) && kymera_IsCurrentUser(entries[i]->user))
        {
            if (entries[i]->callbacks && entries[i]->callbacks->OutputDisconnectRequest &&
                entries[i]->callbacks->OutputDisconnectRequest())
            {
                DEBUG_LOG_DEBUG("kymera_DisconnectUsers: Disconnected enum:output_users_t:%d", entries[i]->user);
                // If the user has accepted the disconnection request he should no longer be current
                PanicFalse(kymera_IsCurrentUser(entries[i]->user) == FALSE);
            }
            else
                status = FALSE;
        }
    }

    return status;
}

static bool kymera_PrepareUser(output_users_t user, const kymera_output_chain_config *chain_config)
{
    bool status = TRUE;

    if (kymera_NoCurrentUsers())
        kymera_CreateOutputChain(user, chain_config);

    if (kymera_IsCurrentChainCompatible(user, chain_config) == FALSE)
    {
        if (kymera_DisconnectUsers(kymera_GetCurrentUsers()))
            kymera_CreateOutputChain(user, chain_config);
        else
            status = FALSE;
    }

    if (status)
        kymera_UpdateInputSampleRate(user, chain_config);

    return status;
}

void Kymera_OutputRegister(const output_registry_entry_t *user_info)
{
    DEBUG_LOG_DEBUG("Kymera_OutputRegister: enum:output_users_t:%d", user_info->user);
    PanicFalse(kymera_IsRegisteredUser(user_info->user) == FALSE);

    if (user_info->creator)
    {
        PanicFalse(user_info->creator->OutputConnect != NULL);
        PanicFalse(user_info->creator->OutputDisconnect != NULL);
    }

    kymera_RegisterUser(user_info);
}

bool Kymera_OutputPrepare(output_users_t user, const kymera_output_chain_config *chain_config)
{
    if (kymera_IsRegisteredUser(user) == FALSE)
        return FALSE;

    if (kymera_PrepareUser(user, chain_config) == FALSE)
        return FALSE;

    DEBUG_LOG_DEBUG("Kymera_OutputPrepare: enum:output_users_t:%d", user);
    state.ready_users |= user;
    return TRUE;
}

bool Kymera_OutputConnect(output_users_t user, const output_source_t *sources)
{
    if (kymera_IsUserConnectable(user) == FALSE)
        return FALSE;

    if (kymera_ConnectToOutputChain(user, sources) == FALSE)
        return FALSE;

    DEBUG_LOG_DEBUG("Kymera_OutputConnect: enum:output_users_t:%d", user);
    state.connected_users |= user;
    return TRUE;
}

void Kymera_OutputDisconnect(output_users_t user)
{
    if (kymera_IsCurrentUser(user))
    {
        bool disconnect = kymera_IsConnectedUser(user);
        state.connected_users &= ~user;
        state.ready_users &= ~user;

        DEBUG_LOG_DEBUG("Kymera_OutputDisconnect: enum:output_users_t:%d", user);

        if (disconnect)
            kymera_DisconnectFromOutputChain(user);

        if (kymera_NoCurrentUsers())
            kymera_DestroyOutputChain();
    }
}

bool Kymera_OutputCreatingCustomChain(output_users_t user)
{
    if ((kymera_IsCustomChainUser(user) == FALSE) || kymera_IsCurrentUser(user))
        return FALSE;

    if (kymera_DisconnectUsers(kymera_GetCurrentUsers()) == FALSE)
        return FALSE;

    DEBUG_LOG_DEBUG("Kymera_OutputCreatingCustomChain: enum:output_users_t:%d", user);
    state.custom_chain_creator = user;
    return TRUE;
}

void Kymera_OutputDestroyingCustomChain(output_users_t user)
{
    if (kymera_IsCurrentCreator(user))
    {
        DEBUG_LOG_DEBUG("Kymera_OutputDestroyingCustomChain: enum:output_users_t:%d", user);
        // All other users must disconnect first in such a scenario
        PanicFalse(kymera_DisconnectUsers(kymera_GetCurrentUsers() & ~user));

        state.custom_chain_creator = output_user_none;
        state.connected_users &= ~user;
        state.ready_users &= ~user;
    }
}

void Kymera_OutputRegisterForIndications(const output_indications_registry_entry_t *user_info)
{
    DEBUG_LOG("Kymera_OutputRegisterForIndications");
    PanicNull((void*)user_info);
    kymera_RegisterForIndications(user_info);
}
unsigned Kymera_OutputGetMainVolumeBufferSize(void)
{
    return state.current_chain_config.source_sync_output_buffer_size_samples;
}

bool Kymera_OutputIsAecAlwaysUsed(void)
{
    bool is_aec_in_forced_config = FALSE;

#ifdef INCLUDE_FIXED_OUTPUT_CHAIN
    is_aec_in_forced_config = Kymera_GetFixedOutputChainConfig()->chain_include_aec;
#endif

    return KymeraOutput_MustAlwaysIncludeAec() || is_aec_in_forced_config;
}
