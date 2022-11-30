/*!
\copyright  Copyright (c) 2020 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\file
\brief      Kymera module to manage MIC connections
*/

#include "kymera_mic_if.h"
#include "kymera_aec.h"
#include "kymera_mic_resampler.h"
#include "kymera_private.h"
#include "kymera_splitter.h"

#define CVC_FRAME_IN_US (7500)
#define MIN_SAMPLE_RATE_IN_KHZ (16)
#define MAX_SAMPLE_RATE_IN_KHZ (32)
#define MAX_CVC_FRAME_SIZE ((CVC_FRAME_IN_US * MAX_SAMPLE_RATE_IN_KHZ) / 1000)

#define MIC_PATH_TRANSFORM_SIZE (1024)
#define AEC_PATH_TRANSFORM_SIZE ((MAX_CVC_FRAME_SIZE / 2) + MIC_PATH_TRANSFORM_SIZE)

#define DEFAULT_TERMINAL_BUFFER_SIZE 15

#define VOICE_AANC_BUFFER_SIZE_MS 45

/*! Registration array for all available users */
typedef struct
{
    unsigned nr_entries;
    const mic_registry_per_user_t* *entry;
} mic_registry_t;

typedef struct
{
    aec_usecase_t aec_usecase;
    aec_audio_config_t  config;
} aec_ref_user_config_t;

typedef void (* SourceFunction) (Source *array, unsigned length_of_array);

static const splitter_config_t splitter_config =
{
    .transform_size_in_words = AEC_PATH_TRANSFORM_SIZE,
    .data_format = operator_data_format_pcm,
};

static const struct
{
    mic_users_t mic_users;
    unsigned leakthrough_enabled:1;
    aec_ref_user_config_t config;
} aec_usecase_map[] =
{
    {mic_user_va, TRUE,
        {.aec_usecase = aec_usecase_va_leakthrough,
            {.ttp_delay = VA_MIC_TTP_LATENCY}
        }
    },
    {mic_user_va, FALSE,
        {.aec_usecase = aec_usecase_voice_assistant,
            {.ttp_delay = VA_MIC_TTP_LATENCY}
        }
    },
    {mic_user_none, TRUE,
        {.aec_usecase = aec_usecase_standalone_leakthrough,
            {.is_source_clock_same = TRUE,
             .buffer_size = DEFAULT_TERMINAL_BUFFER_SIZE
            }
        }
    },
    {mic_user_sco, FALSE,
        {.aec_usecase = aec_usecase_voice_call}
    },
    {(mic_user_sco | mic_user_aanc), FALSE,
        {.aec_usecase = aec_usecase_voice_call,
            {.buffer_size = VOICE_AANC_BUFFER_SIZE_MS}
        }
    },
    {mic_user_le_voice, FALSE,
        {.aec_usecase = aec_usecase_voice_call}
    },
    {(mic_user_le_voice | mic_user_aanc), FALSE,
        {.aec_usecase = aec_usecase_voice_call,
            {.buffer_size = VOICE_AANC_BUFFER_SIZE_MS}
        }
    },
    {mic_user_usb_voice, FALSE,
        {.aec_usecase = aec_usecase_voice_call}
    },
    {(mic_user_usb_voice | mic_user_aanc), FALSE,
        {.aec_usecase = aec_usecase_voice_call,
            {.buffer_size = VOICE_AANC_BUFFER_SIZE_MS}
        }
    },
    {mic_user_aanc, FALSE,
        {.aec_usecase = aec_usecase_adaptive_anc}
    },

    {(mic_user_va | mic_user_aanc), FALSE,
        {.aec_usecase = aec_usecase_voice_assistant}
    },
};

static struct
{
    mic_registry_t registry;
    splitter_handle_t splitter;
    uint32 mic_sample_rate;
    mic_users_t current_users;
    mic_users_t stream_map[MAX_NUM_OF_CONCURRENT_MIC_USERS];
    mic_users_t wake_states;
    unsigned leakthrough_enabled:1;
    unsigned chains_are_awake:1;
    Source mic_sources[MAX_NUM_OF_CONCURRENT_MICS];
    uint8  use_count[MAX_SUPPORTED_MICROPHONES+1];  //mic_ids start with 1
} state =
{
    .registry = {0, NULL},
    .splitter = NULL,
    .mic_sample_rate = 0,
    .current_users = mic_user_none,
    .stream_map = {mic_user_none},
    .wake_states = mic_user_all_mask,
    .leakthrough_enabled = FALSE,
    .chains_are_awake = TRUE,
    .mic_sources = {NULL},
    .use_count = { 0 },
};

static bool kymera_IsMicConcurrencyEnabled(void)
{
    return Kymera_GetChainConfigs()->chain_mic_resampler_config != NULL;
}

static const aec_ref_user_config_t * kymera_GetAecRefUserConfig(mic_users_t users)
{
    unsigned i;
    // Leakthrough is managed via a boolean
    users &= ~mic_user_leakthrough;

    for(i = 0; i < ARRAY_DIM(aec_usecase_map); i++)
    {
        if ((aec_usecase_map[i].mic_users == users) &&
            (aec_usecase_map[i].leakthrough_enabled == state.leakthrough_enabled))
        {
            return &aec_usecase_map[i].config;
        }
    }

    return NULL;
}

static void kymera_SetAecRefUseCase(mic_users_t users)
{
    aec_usecase_t aec_usecase = aec_usecase_default;
    const aec_ref_user_config_t *config = kymera_GetAecRefUserConfig(users);

    if (config)
    {
        aec_usecase = config->aec_usecase;
    }
    Kymera_SetAecUseCase(aec_usecase);
}

static const mic_registry_per_user_t * kymera_GetRegistryEntry(mic_users_t user)
{
    unsigned i;
    for(i = 0; i < state.registry.nr_entries; i++)
    {
        if (state.registry.entry[i]->user == user)
        {
            return state.registry.entry[i];
        }
    }
    DEBUG_LOG_ERROR("kymera_GetRegistryEntry: User not found");
    Panic();
    return NULL;
}

static uint8 kymera_GetStreamIndex(mic_users_t user)
{
    uint8 stream_index;

    for(stream_index = 0; stream_index < ARRAY_DIM(state.stream_map); stream_index++)
    {
        if (state.stream_map[stream_index] == user)
        {
            return stream_index;
        }
    }

    DEBUG_LOG_ERROR("kymera_GetStreamIndex: No stream entry for user enum:mic_users_t:%d", user);
    Panic();
    return 0;
}

static void kymera_ReplaceEntryInStreamMap(mic_users_t old_entry, mic_users_t new_entry)
{
    uint8 stream_index;

    for(stream_index = 0; stream_index < ARRAY_DIM(state.stream_map); stream_index++)
    {
        if (state.stream_map[stream_index] == old_entry)
        {
            state.stream_map[stream_index] = new_entry;
            return;
        }
    }

    DEBUG_LOG_ERROR("kymera_ReplaceEntryInStreamMap: Couldn't find entry enum:mic_users_t:%d", old_entry);
    Panic();
}

static bool kymera_IsCurrentUser(mic_users_t user)
{
    return (state.current_users & user) != 0;
}

static void kymera_AddToCurrentUsers(mic_users_t user)
{
    state.current_users |= user;
}

static void kymera_RemoveFromCurrentUsers(mic_users_t user)
{
    state.current_users &= ~user;
}

static uint8 kymera_GetNrOfCurrentUsers(void)
{
    uint8 users_count = 0;
    mic_users_t users = state.current_users;
    while(users != mic_user_none)
    {
        if ((users & 1) != 0)
        {
            users_count ++;
        }
        users = users >> 1;
    }
    return users_count;
}

static void kymera_AddMicUser(mic_users_t user)
{
    if(!kymera_IsCurrentUser(user))
    {
        kymera_AddToCurrentUsers(user);
        kymera_SetAecRefUseCase(state.current_users);
        kymera_ReplaceEntryInStreamMap(mic_user_none, user);
    }
}

static void kymera_RemoveMicUser(mic_users_t user)
{
    PanicFalse(kymera_IsCurrentUser(user));
    kymera_RemoveFromCurrentUsers(user);
    kymera_SetAecRefUseCase(state.current_users);
    kymera_ReplaceEntryInStreamMap(user, mic_user_none);
}

static void kymera_UnsynchroniseMics(uint8 num_of_mics, const Source *mic_sources)
{
    unsigned i;
    for(i = 0; i < num_of_mics; i++)
    {
        SourceSynchronise(mic_sources[i], NULL);
        DEBUG_LOG("kymera_UnsynchroniseMics: source 0x%x", mic_sources[i]);
    }
}

static void kymera_SynchroniseMics(uint8 num_of_mics, const Source *mic_sources)
{
    unsigned i;
    for(i = 1; i < num_of_mics; i++)
    {
        SourceSynchronise(mic_sources[i-1], mic_sources[i]);
        DEBUG_LOG("kymera_SynchroniseMics: source 0x%x with source 0x%x", mic_sources[i-1], mic_sources[i]);
    }
}

static bool kymera_AreMicsInUse(uint8 num_of_mics, const microphone_number_t *mic_ids)
{
    unsigned i;

    for(i = 0; i < num_of_mics; i++)
    {
        if (Microphones_GetMicrophoneSource(mic_ids[i]) == NULL)
            return FALSE;
    }

    return TRUE;
}

static void kymera_TurnOnMics(uint32 sample_rate, uint8 num_of_mics, const microphone_number_t *mic_ids, Source *mic_sources)
{
    unsigned i;
    bool create_mics = (kymera_AreMicsInUse(num_of_mics, mic_ids) == FALSE);

    if ((create_mics) || (state.mic_sample_rate < (MIN_SAMPLE_RATE_IN_KHZ * 1000)))
    {
        state.mic_sample_rate = sample_rate;
    }

    for(i = 0; i < num_of_mics; i++)
    {
        // Simply increases number of users if mic already created
        mic_sources[i] = Microphones_TurnOnMicrophone(mic_ids[i], state.mic_sample_rate, non_exclusive_user);
        state.use_count[mic_ids[i]]++;
    }

    /* When TurnOnMics is called, the connecting user is not yet added to current_users. */
    if (kymera_GetNrOfCurrentUsers() == 0)
    {
        kymera_UnsynchroniseMics(num_of_mics, mic_sources);
        kymera_SynchroniseMics(num_of_mics, mic_sources);
    }
}

static void kymera_TurnOffMics(uint8 num_of_mics, const microphone_number_t *mic_ids)
{
    unsigned i;

    for(i = 0; i < num_of_mics; i++)
    {
        PanicFalse(state.use_count[mic_ids[i]]>0);
        state.use_count[mic_ids[i]]--;
        Microphones_TurnOffMicrophone(mic_ids[i], non_exclusive_user);
    }

    /* When TurnOffMics is called, the disconnecting user is already removed from current_users. */
    if (kymera_GetNrOfCurrentUsers() == 0)
    {
        kymera_UnsynchroniseMics(num_of_mics, state.mic_sources);
    }
}

static void kymera_PopulateAecConnectParams(uint8 num_of_mics, const Sink *mic_sinks, const Source *mic_sources,
                                            Sink aec_ref_sink, aec_connect_audio_input_t *aec_params)
{
    aec_params->reference_output = aec_ref_sink;

    switch (num_of_mics)
    {
        case 3:
            aec_params->mic_input_3 = mic_sources[2];
            aec_params->mic_output_3 = mic_sinks[2];
        // Fallthrough
        case 2:
            aec_params->mic_input_2 = mic_sources[1];
            aec_params->mic_output_2 = mic_sinks[1];
        // Fallthrough
        case 1:
            aec_params->mic_input_1 = mic_sources[0];
            aec_params->mic_output_1 = mic_sinks[0];
            break;
        default:
            DEBUG_LOG_ERROR("kymera_PopulateAecConnectParams: Unsupported number of mics = %d", num_of_mics);
            Panic();
    }
}

static void kymera_PopulateAecConfig(uint32 sample_rate, aec_audio_config_t *aec_config)
{
    const aec_ref_user_config_t *config = kymera_GetAecRefUserConfig(state.current_users);

    if (config)
    {
        *aec_config = config->config;
    }
    aec_config->mic_sample_rate = sample_rate;
}

static void kymera_ConnectUserDirectlyToAec(uint8 num_of_mics, const Sink *mic_sinks, const Source *mic_sources,
                                            Sink aec_ref_sink)
{
    aec_connect_audio_input_t connect_params = {0};
    aec_audio_config_t config = {0};

    kymera_PopulateAecConnectParams(num_of_mics, mic_sinks, mic_sources, aec_ref_sink, &connect_params);
    kymera_PopulateAecConfig(state.mic_sample_rate, &config);
    Kymera_ConnectAudioInputToAec(&connect_params, &config);
}

static void kymera_ConnectUserToConcurrencyChain(uint8 stream_index, uint8 num_of_mics, const Sink *mic_sinks,
                                                 uint32 sample_rate, Sink aec_ref_sink)
{
    unsigned i;
    bool use_resampler = (sample_rate != state.mic_sample_rate);
    Sink local_sinks[1 + MAX_NUM_OF_CONCURRENT_MICS] = {0};

    if (use_resampler)
    {
        Kymera_MicResamplerCreate(stream_index, state.mic_sample_rate, sample_rate);
        if (aec_ref_sink)
        {
            PanicNull(StreamConnect(Kymera_MicResamplerGetAecOutput(stream_index), aec_ref_sink));
            aec_ref_sink = Kymera_MicResamplerGetAecInput(stream_index);
        }
        for(i = 0; i < num_of_mics; i++)
        {
            if (mic_sinks[i])
            {
                PanicNull(StreamConnect(Kymera_MicResamplerGetMicOutput(stream_index, i), mic_sinks[i]));
                local_sinks[i+1] = Kymera_MicResamplerGetMicInput(stream_index, i);
            }
        }
    }
    else
    {
        memcpy(&local_sinks[1], mic_sinks, num_of_mics * sizeof(local_sinks[0]));
    }

    local_sinks[0] = aec_ref_sink;

    DEBUG_LOG("kymera_ConnectUserToConcurrencyChain");
    Kymera_SplitterConnectToOutputStream(&state.splitter, stream_index, local_sinks);

    if (use_resampler)
    {
        Kymera_MicResamplerStart(stream_index);
    }
}

static void kymera_ConnectSplitterChainToAec(uint8 num_of_mics, const Source *mic_sources)
{
    Sink mic_sinks[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};
    Sink aec_sink = Kymera_SplitterGetInput(&state.splitter, 0);

    for(unsigned i = 0; i < num_of_mics; i++)
    {
        mic_sinks[i] = Kymera_SplitterGetInput(&state.splitter, i+1);
    }

    if (Kymera_AecIsMicPathInputConnected())
    {
        // All mics should have already been connected to AEC reference
        aec_mic_path_output_t connect_params = {0};
        connect_params.aec_reference = aec_sink;
        connect_params.num_of_mics = num_of_mics;
        connect_params.mics = mic_sinks;
        Kymera_ConnectToAecMicPathOutput(&connect_params);
    }
    else
    {
        aec_connect_audio_input_t connect_params = {0};
        aec_audio_config_t config = {0};
        kymera_PopulateAecConfig(state.mic_sample_rate, &config);
        kymera_PopulateAecConnectParams(num_of_mics, mic_sinks, mic_sources, aec_sink, &connect_params);
        Kymera_ConnectAudioInputToAec(&connect_params, &config);
    }
}

static void kymera_ConnectUserViaConcurrencyChain(mic_users_t user, uint8 num_of_sinks, const Sink *mic_sinks,
                                                  uint8 num_of_mics, const Source *mic_sources,
                                                  uint32 sample_rate, Sink aec_ref_sink)
{
    uint8 stream_index = kymera_GetStreamIndex(user);

    if (state.splitter == NULL)
    {
        state.splitter = Kymera_SplitterCreate(MAX_NUM_OF_CONCURRENT_MIC_USERS, 1 + num_of_mics, &splitter_config);
        kymera_ConnectSplitterChainToAec(num_of_mics, mic_sources);
    }

    kymera_ConnectUserToConcurrencyChain(stream_index, num_of_sinks, mic_sinks, sample_rate, aec_ref_sink);

    Kymera_SplitterStartOutputStream(&state.splitter, stream_index);
}

static void kymera_DisconnectUserFromConcurrencyChain(mic_users_t user)
{
    uint8 stream_index = kymera_GetStreamIndex(user);

    Kymera_SplitterDisconnectFromOutputStream(&state.splitter, stream_index);

    if (Kymera_MicResamplerIsCreated(stream_index))
    {
        Kymera_MicResamplerDestroy(stream_index);
    }

    if ((state.current_users & ~mic_user_leakthrough) == user)
    {
        // Destroy splitter and disconnect from AEC since there are no other users
        Kymera_SplitterDestroy(&state.splitter);
    }
}

static void kymera_UserGetConnectionParameters(mic_users_t user, mic_connect_params_t *mic_params, microphone_number_t *mic_ids, Sink *mic_sinks, Sink *aec_ref_sink)
{
    const mic_registry_per_user_t * reg_entry;
    reg_entry = kymera_GetRegistryEntry(user);
    reg_entry->callbacks->MicGetConnectionParameters(mic_ids, mic_sinks, &mic_params->connections.num_of_mics, &mic_params->sample_rate, aec_ref_sink);
}

static void kymera_AddToOrderedListOfMics(microphone_number_t *mic_ids, Sink *mic_sinks, uint8 *num_of_mics, microphone_number_t new_mic_id, Sink new_mic_sink)
{
    PanicFalse(*num_of_mics < MAX_NUM_OF_CONCURRENT_MICS);
    DEBUG_LOG("kymera_AddToOrderedListOfMics: enum:microphone_number_t:%d Sink 0x%x", new_mic_id ,new_mic_sink);

    int8 insert_idx;

    for(insert_idx = (MAX_NUM_OF_CONCURRENT_MICS-1); insert_idx >= 0; insert_idx--)
    {
        if((mic_ids[insert_idx] != microphone_none) && (mic_ids[insert_idx] < new_mic_id))
        {
            break;
        }
    }
    insert_idx++;
    PanicFalse(insert_idx < MAX_NUM_OF_CONCURRENT_MICS);

    for(int8 i = *num_of_mics; i > insert_idx; i--)
    {
        mic_ids[i]   = mic_ids[i-1];
        mic_sinks[i] = mic_sinks[i-1];
    }

    mic_ids[insert_idx] = new_mic_id;
    mic_sinks[insert_idx] = new_mic_sink;
    (*num_of_mics)++;
}

static void kymera_AddMicsToOrderedList(const microphone_number_t *current_mic_ids, const Sink *current_mic_sinks, uint8 current_num_of_mics,
                                        microphone_number_t *ordered_mic_ids, Sink *ordered_mic_sinks, uint8 *ordered_num_of_mics)
{
    for(unsigned i = 0; i < current_num_of_mics; i++)
    {
        PanicFalse(current_mic_ids[i] != microphone_none);

        bool already_available = FALSE;

        for(unsigned j = 0; j < *ordered_num_of_mics; j++)
        {
            // This needs to be cast to avoid an IncompatibleQualified warning by the compiler
            // As this is just comparing by value the warning seems out of place and should be safe to workaround
            if (ordered_mic_ids[j] == (microphone_number_t)current_mic_ids[i])
            {
                already_available = TRUE;
                break;
            }
        }

        if(!already_available)
        {
            kymera_AddToOrderedListOfMics(ordered_mic_ids, ordered_mic_sinks, ordered_num_of_mics,
                                          current_mic_ids[i], current_mic_sinks[i]);
        }
    }
}

static void kymera_AddMandatoryMicsFromUser(mic_users_t user, microphone_number_t *mic_ids, Sink *mic_sinks, uint8 *num_of_mics)
{
    Sink empty_mic_sinks[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};
    const mic_registry_per_user_t * reg_entry;
    reg_entry = kymera_GetRegistryEntry(user);

    DEBUG_LOG("kymera_AddMandatoryMicsFromUser: Checking for enum:mic_users_t:%d", user);
    kymera_AddMicsToOrderedList(reg_entry->mandatory_mic_ids, empty_mic_sinks, reg_entry->num_of_mandatory_mics, mic_ids, mic_sinks, num_of_mics);
}

static void kymera_AddMandatoryMicsFromAllUsers(microphone_number_t *mic_ids, Sink *mic_sinks, uint8 *num_of_mics)
{
    for(uint8 user_index = 0; user_index < state.registry.nr_entries; user_index++)
    {
        mic_users_t current_user = state.registry.entry[user_index]->user;
        kymera_AddMandatoryMicsFromUser(current_user, mic_ids, mic_sinks, num_of_mics);
    }
}

/* Read out the current microphone configuration, return the microphone numbers */
static uint8 kymera_GetMicIdsInUse(microphone_number_t *mic_ids)
{
    uint8 num_mics_in_use = 0;
    for(uint8 mic_id = microphone_1; mic_id <= MAX_SUPPORTED_MICROPHONES; mic_id++)
    {
        if (state.use_count[mic_id] > 0)
        {
            mic_ids[num_mics_in_use] = mic_id;
            num_mics_in_use++;
            DEBUG_LOG("kymera_GetMicIdsInUse: enum:microphone_number_t:%d is in use", mic_id);
            PanicFalse(num_mics_in_use <= MAX_NUM_OF_CONCURRENT_MICS);
        }
    }
    return num_mics_in_use;
}

static void kymera_PopulateMicSources(mic_users_t user, const mic_connect_params_t *mic_params, mic_users_t reconnect_users,
                                      microphone_number_t *combined_mic_ids, Sink *combined_mic_sinks,
                                      uint8 *combined_num_of_mics, uint32 *combined_sample_rate)
{
    mic_connect_params_t current_mic_params = { 0 };
    microphone_number_t current_mic_ids[MAX_NUM_OF_CONCURRENT_MICS] = {0};
    Sink current_mic_sinks[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};
    current_mic_params.connections.mic_ids = current_mic_ids;
    current_mic_params.connections.mic_sinks = current_mic_sinks;

    Sink empty_mic_sinks[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};

    if (user != mic_user_none)
    {
        /* Populate combined array with sorted mic_params if available */
        *combined_sample_rate = MAX(mic_params->sample_rate, (MIN_SAMPLE_RATE_IN_KHZ * 1000));
        kymera_AddMicsToOrderedList(mic_params->connections.mic_ids, mic_params->connections.mic_sinks, mic_params->connections.num_of_mics,
                                    combined_mic_ids, combined_mic_sinks, combined_num_of_mics);

        microphone_number_t mic_ids_in_use[MAX_NUM_OF_CONCURRENT_MICS] = {microphone_none};
        uint8 num_mics_in_use = kymera_GetMicIdsInUse(mic_ids_in_use);
        if (num_mics_in_use > 0)
        {
            /* Add each activated mic to combined array with sink NULL */
            kymera_AddMicsToOrderedList(mic_ids_in_use, empty_mic_sinks, num_mics_in_use,
                                        combined_mic_ids, combined_mic_sinks, combined_num_of_mics);
        }
    }

    if (reconnect_users != mic_user_none)
    {
        /* Collect mic params from all users that want to be reconnected */
        for(uint8 user_index = 0; user_index < state.registry.nr_entries; user_index++)
        {
            mic_users_t current_user = state.registry.entry[user_index]->user;
            if (((reconnect_users & current_user) != 0) && (current_user != user))
            {
                DEBUG_LOG("kymera_PopulateMicSources: Adding reconnect info from enum:mic_users_t:%d",current_user);
                Sink aec_ref_sink;
                kymera_UserGetConnectionParameters(current_user, &current_mic_params, current_mic_ids, current_mic_sinks, &aec_ref_sink);
                *combined_sample_rate = MAX(*combined_sample_rate, MAX(current_mic_params.sample_rate, (MIN_SAMPLE_RATE_IN_KHZ * 1000)));
                /* Add mics from reconnect_users to combined array with sink NULL */
                kymera_AddMicsToOrderedList(current_mic_ids, empty_mic_sinks, current_mic_params.connections.num_of_mics,
                                            combined_mic_ids, combined_mic_sinks, combined_num_of_mics);
            }
        }
    }
    kymera_AddMandatoryMicsFromAllUsers(combined_mic_ids, combined_mic_sinks, combined_num_of_mics);
}

static bool kymera_UserDisconnectIndication(mic_users_t user, mic_change_info_t *info)
{
    bool want_to_reconnect = FALSE;
    const mic_registry_per_user_t * reg_entry;
    reg_entry = kymera_GetRegistryEntry(user);
    if (reg_entry->callbacks->MicDisconnectIndication)
    {
        DEBUG_LOG("kymera_UserDisconnectIndication: informing enum:mic_users_t:%d, enum:mic_event_t:%d", user, info->event);
        want_to_reconnect = reg_entry->callbacks->MicDisconnectIndication(info);
        DEBUG_LOG("kymera_UserDisconnectIndication: enum:mic_users_t:%d want_to_reconnect=%d", user, want_to_reconnect);
    }
    return want_to_reconnect;
}

static void kymera_UserReadyForReconnectionIndication(mic_users_t user, mic_change_info_t *info)
{
    const mic_registry_per_user_t * reg_entry;
    reg_entry = kymera_GetRegistryEntry(user);
    if (reg_entry->callbacks->MicReadyForReconnectionIndication)
    {
        DEBUG_LOG("kymera_UserReadyForReconnectionIndication: enum:mic_users_t:%d enum:mic_event_t:%d",
                  user, info->event);
        reg_entry->callbacks->MicReadyForReconnectionIndication(info);
    }
}

static void kymera_UserUpdatedStateIndication(mic_users_t user)
{
    const mic_registry_per_user_t * reg_entry;
    reg_entry = kymera_GetRegistryEntry(user);
    if (reg_entry->callbacks->MicUserUpdatedState)
    {
        DEBUG_LOG("kymera_UserUpdatedStateIndication: enum:mic_users_t:%d", user);
        reg_entry->callbacks->MicUserUpdatedState();
    }
}

static void kymera_MicUserChangePendingNotification(mic_users_t user, mic_change_info_t *info)
{
    const mic_registry_per_user_t * reg_entry;
    reg_entry = kymera_GetRegistryEntry(user);
    if (reg_entry->callbacks->MicUserChangePendingNotification)
    {
        DEBUG_LOG("kymera_MicUserChangePendingNotification: sent to enum:mic_users_t:%d", user);
        reg_entry->callbacks->MicUserChangePendingNotification(info);
    }
}

static void kymera_SendMicUserChangePendingNotificationToAllUsers(mic_users_t user, mic_change_info_t *info)
{
    uint8 user_index;
    mic_users_t current_user;

    for(user_index = 0; user_index < state.registry.nr_entries; user_index++)
    {
        current_user = state.registry.entry[user_index]->user;
        if((kymera_IsCurrentUser(current_user)) && (current_user != user))
        {
            kymera_MicUserChangePendingNotification(current_user, info);
        }
    }
}

static void kymera_UserReconnectedIndication(mic_users_t user)
{
    const mic_registry_per_user_t * reg_entry;
    reg_entry = kymera_GetRegistryEntry(user);
    if (reg_entry->callbacks->MicReconnectedIndication)
    {
        DEBUG_LOG("kymera_UserReconnectedIndication: user enum:mic_users_t:%d reconnected", user);
        reg_entry->callbacks->MicReconnectedIndication();
    }
}

static void kymera_SendReconnectedIndicationToAllUsers(mic_users_t reconnected_users)
{
    uint8 user_index;
    mic_users_t current_user;

    if (kymera_IsMicConcurrencyEnabled())
    {
        if (reconnected_users != mic_user_none)
        {
            for(user_index = 0; user_index < state.registry.nr_entries; user_index++)
            {
                current_user = state.registry.entry[user_index]->user;
                if ((reconnected_users & current_user) != 0)
                {
                    kymera_UserReconnectedIndication(current_user);
                }
            }
        }
    }
}

static void kymera_ConnectLeakthrough(const mic_connect_params_t *mic_params, Source *mic_sources)
{
    kymera_TurnOnMics(mic_params->sample_rate,
                      mic_params->connections.num_of_mics,
                      mic_params->connections.mic_ids,
                      mic_sources);

    if(state.current_users == mic_user_none)
    {
        aec_audio_config_t config = {0};
        aec_mic_path_input_t connect_params = {0};
        connect_params.num_of_mics = mic_params->connections.num_of_mics;
        connect_params.mics = mic_sources;
        kymera_PopulateAecConfig(state.mic_sample_rate, &config);
        kymera_SetAecRefUseCase(state.current_users);
        Kymera_ConnectToAecMicPathInput(&connect_params, &config);
    }

    kymera_AddToCurrentUsers(mic_user_leakthrough);
}

static void kymera_ReconnectAllUsers(mic_users_t reconnect_users, Source *mic_sources)
{
    /* Reconnect all others */
    unsigned user_index;
    mic_users_t current_user;
    Sink aec_ref_sink;

    if (reconnect_users != mic_user_none)
    {
        for(user_index = 0; user_index < state.registry.nr_entries; user_index++)
        {
            current_user = state.registry.entry[user_index]->user;
            if ((reconnect_users & current_user) != 0)
            {
                mic_connect_params_t local_mic_params = { 0 };
                microphone_number_t mic_ids[MAX_NUM_OF_CONCURRENT_MICS] = {0};
                Sink mic_sinks[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};
                local_mic_params.connections.mic_ids = mic_ids;
                local_mic_params.connections.mic_sinks = mic_sinks;

                DEBUG_LOG("kymera_ReconnectAllUsers: enum:mic_users_t:%d", current_user);
                kymera_UserGetConnectionParameters(current_user, &local_mic_params, mic_ids, mic_sinks, &aec_ref_sink);
                if(current_user != mic_user_leakthrough)
                {
                    mic_connect_params_t combined_mic_params = { 0 };
                    microphone_number_t combined_mic_ids[MAX_NUM_OF_CONCURRENT_MICS] = {0};
                    Sink combined_mic_sinks[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};
                    combined_mic_params.connections.mic_ids = combined_mic_ids;
                    combined_mic_params.connections.mic_sinks = combined_mic_sinks;

                    kymera_PopulateMicSources(current_user, &local_mic_params, reconnect_users, combined_mic_ids, combined_mic_sinks,
                                              &combined_mic_params.connections.num_of_mics, &combined_mic_params.sample_rate);
                    /* Register each non-exclusive user, mics are already turned on */
                    kymera_TurnOnMics(combined_mic_params.sample_rate, combined_mic_params.connections.num_of_mics,
                                      combined_mic_params.connections.mic_ids, mic_sources);
                    kymera_AddMicUser(current_user);
                    kymera_ConnectUserViaConcurrencyChain(current_user, combined_mic_params.connections.num_of_mics, combined_mic_params.connections.mic_sinks,
                                                          combined_mic_params.connections.num_of_mics, mic_sources,
                                                          combined_mic_params.sample_rate, aec_ref_sink);
                }
                else
                {
                    kymera_ConnectLeakthrough(&local_mic_params, mic_sources);
                }
            }
        }
    }
}

static void kymera_ConnectUserToMics(mic_users_t user, const mic_connect_params_t *mic_params, Sink aec_ref_sink, mic_users_t reconnect_users)
{
    mic_connect_params_t local_mic_params = { 0 };
    microphone_number_t local_mic_ids[MAX_NUM_OF_CONCURRENT_MICS] = {0};
    Sink local_mic_sinks[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};
    local_mic_params.connections.mic_ids = local_mic_ids;
    local_mic_params.connections.mic_sinks = local_mic_sinks;
    uint8 combined_num_of_mics = 0;

    if (kymera_IsMicConcurrencyEnabled())
    {
        kymera_PopulateMicSources(user, mic_params, reconnect_users, local_mic_ids, local_mic_sinks,
                                  &combined_num_of_mics, &local_mic_params.sample_rate);
        kymera_TurnOnMics(local_mic_params.sample_rate, combined_num_of_mics,
                          local_mic_params.connections.mic_ids, state.mic_sources);

        /* Connect new user first */
        kymera_AddMicUser(user);
        kymera_ConnectUserViaConcurrencyChain(user, combined_num_of_mics, local_mic_params.connections.mic_sinks,
                                              combined_num_of_mics, state.mic_sources,
                                              mic_params->sample_rate, aec_ref_sink);

        kymera_ReconnectAllUsers(reconnect_users, state.mic_sources);
        kymera_SendReconnectedIndicationToAllUsers(reconnect_users);
    }
    else
    {
        DEBUG_LOG("kymera_ConnectUserToMics: Concurrency disabled. Using legacy mode");
        kymera_TurnOnMics(mic_params->sample_rate, mic_params->connections.num_of_mics, mic_params->connections.mic_ids, state.mic_sources);
        kymera_ConnectUserDirectlyToAec(mic_params->connections.num_of_mics, mic_params->connections.mic_sinks, state.mic_sources, aec_ref_sink);
    }
}

static mic_users_t kymera_InformUsersAboutDisconnection(mic_users_t user, mic_change_info_t *info)
{
    uint8 user_index;
    mic_users_t current_user;
    bool reconnect_request;
    mic_users_t reconnect_users = 0;

    /* Inform active users about disconnection */
    for(user_index = 0; user_index < state.registry.nr_entries; user_index++)
    {
        current_user = state.registry.entry[user_index]->user;
        if((kymera_IsCurrentUser(current_user)) && (current_user != user))
        {
            /* Active user found -> send disconnect indication */
            reconnect_request = kymera_UserDisconnectIndication(current_user, info);
            if (reconnect_request)
            {
                /* Collect which user wants to be reconnected */
                reconnect_users |= current_user;
            }
            else
            {
                /* Inform others if a user does not want to be reconnected again */
                info->user = current_user;
                info->event = mic_event_disconnecting;
                kymera_SendMicUserChangePendingNotificationToAllUsers(current_user, info);
            }
        }
    }
    return reconnect_users;
}

static void kymera_InformUsersAboutReadyForReconnection(mic_users_t new_user, mic_users_t users_to_be_informed, mic_change_info_t *info)
{
    uint8 user_index;
    mic_users_t current_user;

    /* Inform active users about ReadyForReconnection */
    for(user_index = 0; user_index < state.registry.nr_entries; user_index++)
    {
        current_user = state.registry.entry[user_index]->user;
        if(((current_user & users_to_be_informed) != mic_user_none) && (current_user != new_user))
        {
            /* user found -> send ReadyForReconnection indication */
            kymera_UserReadyForReconnectionIndication(current_user, info);
        }
    }
}

static void kymera_SendUpdatedStateIndication(mic_users_t user)
{
    uint8 user_index;
    mic_users_t current_user;

    /* Inform active users about a change in state */
    for(user_index = 0; user_index < state.registry.nr_entries; user_index++)
    {
        current_user = state.registry.entry[user_index]->user;
        if((kymera_IsCurrentUser(current_user)) && (current_user != user))
        {
            kymera_UserUpdatedStateIndication(current_user);
        }
    }
}

static mic_user_state_t kymera_GetStateFromRemainingUsers(mic_users_t user)
{
    /* Interruptible is set as default.
     * If any user is in state always_interrupt, default will be overwritten with always_interrupt. Checking continues.
     * When the first non_interruptible user is found, the function will return directly with state non_interruptible.
     */
    uint8 user_index;
    mic_users_t current_user;
    mic_user_state_t current_state;
    mic_user_state_t mic_user_state = mic_user_state_interruptible;

    for(user_index = 0; user_index < state.registry.nr_entries; user_index++)
    {
        current_user = state.registry.entry[user_index]->user;
        if((kymera_IsCurrentUser(current_user)) && (current_user != user))
        {
            current_state = *state.registry.entry[user_index]->mic_user_state;
            if (current_state == mic_user_state_non_interruptible)
            {
                return current_state;
            }
            else if (current_state == mic_user_state_always_interrupt)
            {
                mic_user_state = mic_user_state_always_interrupt;
            }
        }
    }
    return mic_user_state;
}

static void kymera_DisconnectAllUsers(void)
{
    uint8 user_index;
    mic_users_t current_user;

    for(user_index = 0; user_index < state.registry.nr_entries; user_index++)
    {
        current_user = state.registry.entry[user_index]->user;
        if(kymera_IsCurrentUser(current_user))
        {
            mic_connect_params_t current_mic_params = { 0 };
            microphone_number_t current_mic_ids[MAX_NUM_OF_CONCURRENT_MICS] = {0};
            Sink current_mic_sinks[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};
            current_mic_params.connections.mic_ids = current_mic_ids;
            current_mic_params.connections.mic_sinks = current_mic_sinks;

            mic_connect_params_t combined_mic_params = { 0 };
            microphone_number_t combined_mic_ids[MAX_NUM_OF_CONCURRENT_MICS] = {0};
            Sink combined_mic_sinks[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};
            combined_mic_params.connections.mic_ids = combined_mic_ids;
            combined_mic_params.connections.mic_sinks = combined_mic_sinks;

            DEBUG_LOG("kymera_DisconnectAllUsers: enum:mic_users_t:%d", current_user);
            Sink aec_ref_sink;
            kymera_UserGetConnectionParameters(current_user, &current_mic_params, current_mic_ids, current_mic_sinks, &aec_ref_sink);
            kymera_AddMicsToOrderedList(current_mic_ids, current_mic_sinks, current_mic_params.connections.num_of_mics,
                                        combined_mic_ids, combined_mic_sinks, &combined_mic_params.connections.num_of_mics);

            if(current_user != mic_user_leakthrough)
            {
                kymera_DisconnectUserFromConcurrencyChain(current_user);
                kymera_RemoveMicUser(current_user);

                /* Collect mics and call TurnOffMics to decrement non_exclusive user */
                microphone_number_t mic_ids_in_use[MAX_NUM_OF_CONCURRENT_MICS] = {microphone_none};
                Sink empty_mic_sinks[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};
                uint8 num_mics_in_use = kymera_GetMicIdsInUse(mic_ids_in_use);
                if (num_mics_in_use > 0)
                {
                    /* The sinks for the current_user are already set in combined_mic_sinks.
                       Add each activated mic to combined array with sink NULL */
                    kymera_AddMicsToOrderedList(mic_ids_in_use, empty_mic_sinks, num_mics_in_use,
                                                combined_mic_ids, combined_mic_sinks, &combined_mic_params.connections.num_of_mics);
                }
            }
            else
            {
                kymera_RemoveFromCurrentUsers(current_user);
            }
            kymera_TurnOffMics(combined_mic_params.connections.num_of_mics, combined_mic_ids);
        }
    }
    Kymera_DisconnectAudioInputFromAec();
}

static bool kymera_PrepareForConnection(mic_users_t new_user, const mic_connect_params_t *mic_params, mic_users_t *reconnect_users)
{
    mic_change_info_t info = {0};
    mic_user_state_t remaining_user_state;
    mic_users_t inform_users;

    if (state.current_users == mic_user_none)
        return TRUE;

    /* If concurrency is enabled:
           1) Mics in use must include all mics requested (you can only sync mics and connect to AEC REF once)
           2) Sample rate requested cannot be higher than sample rate of mics in use
    */
    if (kymera_IsMicConcurrencyEnabled())
    {
        info.user = new_user;
        info.event = mic_event_connecting;
        if (state.mic_sample_rate < mic_params->sample_rate)
        {
            info.event |= mic_event_higher_sample_rate;
        }
        if (!kymera_AreMicsInUse(mic_params->connections.num_of_mics, mic_params->connections.mic_ids))
        {
            info.event |= mic_event_extra_mic;
        }
        kymera_SendMicUserChangePendingNotificationToAllUsers(new_user, &info);
        remaining_user_state = kymera_GetStateFromRemainingUsers(new_user);
        if ((info.event == mic_event_connecting) && (remaining_user_state != mic_user_state_always_interrupt))
        {
            /* No change in sample rate or extra mic necessary. Connection is possible */
            /* In case always_interrupt: User wants to be disconnected / reconnected every time -> skip the return */
            return TRUE;
        }

        /* Conflicting mic params detected or state = always_interrupt */
        if ((remaining_user_state == mic_user_state_interruptible) ||
            (remaining_user_state == mic_user_state_always_interrupt))
        {
            DEBUG_LOG("kymera_PrepareForConnection: Connection is possible. enum:mic_user_state_t:%d enum:mic_event_t:%d",
                      remaining_user_state, info.event);
            *reconnect_users = kymera_InformUsersAboutDisconnection(new_user, &info);
            inform_users = state.current_users;
            kymera_DisconnectAllUsers();
            kymera_InformUsersAboutReadyForReconnection(new_user, inform_users, &info);
            return TRUE;
        }
        /* At least one user is non_interruptible */
        DEBUG_LOG("kymera_PrepareForConnection: Conflict detected. Connection is not possible.");
        return FALSE;
    }
    return TRUE;
}

static bool kymera_PrepareForDisconnection(mic_users_t user, mic_change_info_t *info, mic_users_t *reconnect_users)
{
    mic_user_state_t mic_user_state;

    if (kymera_IsMicConcurrencyEnabled())
    {
        /* If concurrency is enabled:
               1) Disconnection of mics is done when only one user is active
               2) If more than one user is active, the remaining users are checked for interruptibility.
                  - If all remaining users are interruptible, reconnection is done
                  - If at least one remaining user is non-interruptible no reconnection is allowed
        */

        /* User to be disconnected is the only user -> disconnect mics */
        if (state.current_users == user)
            return TRUE;

        kymera_SendMicUserChangePendingNotificationToAllUsers(user, info);
        mic_user_state = kymera_GetStateFromRemainingUsers(user);
        if ((mic_user_state == mic_user_state_interruptible) ||
            (mic_user_state == mic_user_state_always_interrupt))
        {
            *reconnect_users = kymera_InformUsersAboutDisconnection(user, info);
            return TRUE;
        }
        /* At least one user is non_interruptible: Do not disconnect the mics */
        return FALSE;
    }
    /* If concurrency is disabled:
          Only single client connections are expected.
          A disconnection is always allowed.
    */
    return TRUE;
}

static void kymera_PreserveSources(Source *array, unsigned length_of_array)
{
    PanicFalse(OperatorFrameworkPreserve(0, NULL, length_of_array, array, 0, NULL));
}

static void kymera_ReleaseSources(Source *array, unsigned length_of_array)
{
    PanicFalse(OperatorFrameworkRelease(0, NULL, length_of_array, array, 0, NULL));
}

static void kymera_RunOnAllMics(SourceFunction function)
{
    uint8 number_of_mics = 0;
    Source source, mics[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};

    for(uint8 mic_id = microphone_1; mic_id <= MAX_SUPPORTED_MICROPHONES; mic_id++)
    {
        if(state.use_count[mic_id] > 0)
        {
            source = PanicNull(Microphones_GetMicrophoneSource(mic_id));
            PanicFalse(number_of_mics < MAX_NUM_OF_CONCURRENT_MICS);
            mics[number_of_mics] = source;
            number_of_mics++;
        }
    }

    function(mics, number_of_mics);
}

static void kymera_Sleep(void)
{
    bool result = state.current_users != 0 &&                           /* At least one user */
                 (state.wake_states & (state.current_users)) == 0;      /* and they're all asleep */
    if (result && state.chains_are_awake)
    {
        kymera_RunOnAllMics(kymera_PreserveSources);
        Kymera_MicResamplerSleep();
        Kymera_SplitterSleep(&state.splitter);
        Kymera_AecSleep();
        state.chains_are_awake = FALSE;
    }
}

static void kymera_Wake(void)
{
    if (!state.chains_are_awake)
    {
        Kymera_AecWake();
        Kymera_SplitterWake(&state.splitter);
        Kymera_MicResamplerWake();
        kymera_RunOnAllMics(kymera_ReleaseSources);
        state.chains_are_awake = TRUE;
    }
}

void Kymera_MicUserUpdatedState(mic_users_t user)
{
    kymera_SendUpdatedStateIndication(user);
}

void Kymera_MicRegisterUser(const mic_registry_per_user_t * const info)
{
    DEBUG_LOG("Kymera_MicRegisterUser: enum:mic_users_t:%d", info->user);

    state.registry.entry = PanicNull(realloc(state.registry.entry, (state.registry.nr_entries + 1) * sizeof(*state.registry.entry)));

    PanicNull((void*)info->callbacks);
    PanicNull((void*)info->callbacks->MicGetConnectionParameters);  /* This callback is mandatory, the others will be sent if available */

    state.registry.entry[state.registry.nr_entries] = info;
    state.registry.nr_entries++;
}

mic_users_t Kymera_MicGetActiveUsers(void)
{
    DEBUG_LOG("Kymera_MicGetActiveUsers: 0x%x",state.current_users);
    return state.current_users;
}

bool Kymera_MicConnect(mic_users_t user)
{
    bool connection_possible;
    mic_users_t reconnect_users = mic_user_none;
    Sink aec_ref_sink;
    uint8 i;

    mic_connect_params_t local_mic_params = { 0 };
    microphone_number_t local_mic_ids[MAX_NUM_OF_CONCURRENT_MICS] = {0};
    Sink local_mic_sinks[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};
    local_mic_params.connections.mic_ids = local_mic_ids;
    local_mic_params.connections.mic_sinks = local_mic_sinks;

    kymera_Wake();

    DEBUG_LOG("Kymera_MicConnect received from user enum:mic_users_t:%d", user);
    if (MAX_NUM_OF_CONCURRENT_MIC_USERS <= kymera_GetNrOfCurrentUsers())
    {
        return FALSE;
    }
    kymera_UserGetConnectionParameters(user, &local_mic_params, local_mic_ids, local_mic_sinks, &aec_ref_sink);
    PanicFalse((local_mic_params.connections.num_of_mics > 0) && (local_mic_params.connections.num_of_mics <= MAX_NUM_OF_CONCURRENT_MICS));

    connection_possible = kymera_PrepareForConnection(user, &local_mic_params, &reconnect_users);
    if (connection_possible)
    {
        for(i = 0; i < local_mic_params.connections.num_of_mics; i++)
        {
            DEBUG_LOG("Kymera_MicConnect: - enum:microphone_number_t:%d Sink: 0x%x", local_mic_ids[i], local_mic_sinks[i]);
            PanicFalse(local_mic_ids[i] != microphone_none);    // Expect receiving valid microphones from client
            PanicFalse(local_mic_sinks[i] != NULL);             // Expect receiving valid sinks for each microphone
        }
        DEBUG_LOG("Kymera_MicConnect: - aec_ref_sink: 0x%x", aec_ref_sink);
        kymera_ConnectUserToMics(user, &local_mic_params, aec_ref_sink, reconnect_users);
    }
    else
    {
        DEBUG_LOG("Kymera_MicConnect: Connection for user enum:mic_users_t:%d rejected, try again later.", user);
    }
    kymera_Sleep();
    return connection_possible;
}

void Kymera_MicDisconnect(mic_users_t user)
{
    mic_connect_params_t local_mic_params = { 0 };
    microphone_number_t local_mic_ids[MAX_NUM_OF_CONCURRENT_MICS] = {0};
    Sink local_mic_sinks[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};
    local_mic_params.connections.mic_ids = local_mic_ids;
    local_mic_params.connections.mic_sinks = local_mic_sinks;
    mic_users_t inform_users;
    mic_users_t reconnect_users = mic_user_none;
    bool disconnect_mics;
    mic_change_info_t info = {0};

    kymera_Wake();

    DEBUG_LOG("Kymera_MicDisconnect: received from enum:mic_users_t:%d", user);
    info.user = user;
    info.event = mic_event_disconnecting;
    disconnect_mics = kymera_PrepareForDisconnection(user, &info, &reconnect_users);

    if (disconnect_mics)
    {
        inform_users = state.current_users;
        kymera_DisconnectAllUsers();
        kymera_InformUsersAboutReadyForReconnection(user, inform_users, &info);
    }
    else
    {
        DEBUG_LOG("Kymera_MicDisconnect: User enum:mic_users_t:%d disconnects but mics are not removed", user);
        kymera_DisconnectUserFromConcurrencyChain(user);
        kymera_RemoveMicUser(user);

        /* Collect mics and call TurnOffMics to decrement non_exclusive user */
        microphone_number_t mic_ids_in_use[MAX_NUM_OF_CONCURRENT_MICS] = {microphone_none};
        uint8 num_mics_in_use = kymera_GetMicIdsInUse(mic_ids_in_use);
        if (num_mics_in_use > 0)
        {
            /* The sinks for the current_user are already set in mic_sinks.
               Add each activated mic to combined array with sink NULL */
            Sink empty_mic_sinks[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};
            kymera_AddMicsToOrderedList(mic_ids_in_use, empty_mic_sinks, num_mics_in_use,
                                        local_mic_ids, local_mic_sinks, &local_mic_params.connections.num_of_mics);
        }
        kymera_TurnOffMics(local_mic_params.connections.num_of_mics, local_mic_params.connections.mic_ids);
    }

    if (reconnect_users != mic_user_none)
    {
        kymera_ReconnectAllUsers(reconnect_users, state.mic_sources);
        kymera_SendReconnectedIndicationToAllUsers(reconnect_users);
    }
    kymera_Sleep();
}

void Kymera_MicSleep(mic_users_t user)
{
    state.wake_states &= ~user;
    kymera_Sleep();
}

void Kymera_MicWake(mic_users_t user)
{
    if (kymera_IsCurrentUser(user))
    {
        state.wake_states |= user;
        kymera_Wake();
    }
}

#ifdef ENABLE_AEC_LEAKTHROUGH
void Kymera_MicAttachLeakthrough(mic_users_t user, const mic_connect_params_t *mic_params)
{
    DEBUG_LOG("Kymera_MicAttachLeakthrough: enum:mic_users_t:%d", user);

    Source mic_sources[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};

    state.leakthrough_enabled = TRUE;

    kymera_Wake();
    kymera_ConnectLeakthrough(mic_params, mic_sources);
    kymera_Sleep();
}

void Kymera_MicDetachLeakthrough(mic_users_t user)
{
    DEBUG_LOG("Kymera_MicDetachLeakthrough: enum:mic_users_t:%d", user);

    Sink aec_ref_sink;
    mic_connect_params_t local_mic_params = { 0 };
    microphone_number_t local_mic_ids[MAX_NUM_OF_CONCURRENT_MICS] = {0};
    Sink local_mic_sinks[MAX_NUM_OF_CONCURRENT_MICS] = {NULL};

    local_mic_params.connections.mic_ids = local_mic_ids;
    local_mic_params.connections.mic_sinks = local_mic_sinks;

    kymera_Wake();

    kymera_UserGetConnectionParameters(user, &local_mic_params, local_mic_ids, local_mic_sinks, &aec_ref_sink);
    kymera_RemoveFromCurrentUsers(user);
    if(state.current_users == mic_user_none)
    {
        Kymera_DisconnectAudioInputFromAec();
    }
    kymera_TurnOffMics(local_mic_params.connections.num_of_mics, local_mic_params.connections.mic_ids);

    kymera_Sleep();

    state.leakthrough_enabled = FALSE;
}
#endif

#ifdef HOSTED_TEST_ENVIRONMENT
void Kymera_MicClearState(void)
{
    uint8 i;

    if (state.registry.entry)
    {
        DEBUG_LOG("Kymera_MicClearState: Registry %p with %d entries", state.registry.entry, state.registry.nr_entries);
        free(state.registry.entry);
        state.registry.entry = NULL;
        state.registry.nr_entries = 0;
    }
    state.current_users = mic_user_none;
    state.leakthrough_enabled = FALSE;
    state.mic_sample_rate = 0;
    for(i = 0; i < MAX_NUM_OF_CONCURRENT_MIC_USERS; i++)
    {
        state.stream_map[i] = mic_user_none;
    }
    state.splitter = NULL;
    state.wake_states = mic_user_all_mask;
    state.chains_are_awake = TRUE;
    for(i = 0 ; i < MAX_NUM_OF_CONCURRENT_MICS; i++)
    {
        state.mic_sources[i] = NULL;
    }
    for(i = 0 ; i < MAX_SUPPORTED_MICROPHONES; i++)
    {
        state.use_count[i] = 0;
    }
}

Sink Kymera_MicGetAecSplitterConnection(uint8 stream_index)
{
    Sink connected_sink = NULL;
    uint8 num_of_inputs;

    num_of_inputs = Kymera_SplitterGetNumOfInputs(state.splitter);
    if (num_of_inputs > 0)
    {
        connected_sink = Kymera_SplitterGetSink(state.splitter, stream_index, 0);
        DEBUG_LOG("Kymera_MicGetAecSplitterConnection stream_index %d channel[0] connected_sink 0x%x",
                  stream_index, connected_sink);
    }
    return connected_sink;
}

Sink Kymera_MicGetMicSplitterConnection(uint8 stream_index, uint8 channel)
{
    Sink connected_sink = NULL;
    uint8 num_of_inputs;
    uint8 mic_channel = channel + 1;

    num_of_inputs = Kymera_SplitterGetNumOfInputs(state.splitter);
    if (mic_channel <= num_of_inputs)
    {
        connected_sink = Kymera_SplitterGetSink(state.splitter, stream_index, mic_channel);
        DEBUG_LOG("Kymera_MicGetMicSplitterConnection stream_index %d channel[%d] connected_sink 0x%x",
                  stream_index, mic_channel, connected_sink);
    }
    return connected_sink;
}
#endif    //HOSTED_TEST_ENVIRONMENT
