/*!
\copyright  Copyright (c) 2019-2020 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\file
\brief      HFP library link priority to Voice Source mappings.
*/

#include "hfp_profile_voice_source_link_prio_mapping.h"

#include <logging.h>
#include <panic.h>

hfp_link_prio_mapping_t hfp_link_prio_mappings;

hfp_link_priority HfpProfile_GetHfpLinkPrioForVoiceSource(voice_source_t source)
{
    PanicFalse(source < max_voice_sources);
    return hfp_link_prio_mappings.voice_source_to_link_prio_map[source];
}

voice_source_t HfpProfile_GetVoiceSourceForHfpLinkPrio(hfp_link_priority priority)
{
    PanicFalse(priority <= hfp_secondary_link);
    return hfp_link_prio_mappings.link_prio_to_voice_source_map[priority];
}

void HfpProfile_UpdateMappingOnConnection(voice_source_t new_source)
{
    PanicFalse(new_source == voice_source_hfp_1 || new_source == voice_source_hfp_2);

    if (hfp_link_prio_mappings.link_prio_to_voice_source_map[hfp_primary_link] == voice_source_none)
    {
        hfp_link_prio_mappings.link_prio_to_voice_source_map[hfp_primary_link] = new_source;
        hfp_link_prio_mappings.voice_source_to_link_prio_map[new_source] = hfp_primary_link;
    }
    else if (hfp_link_prio_mappings.link_prio_to_voice_source_map[hfp_primary_link] != new_source)
    {
        hfp_link_prio_mappings.link_prio_to_voice_source_map[hfp_secondary_link] = new_source;
        hfp_link_prio_mappings.voice_source_to_link_prio_map[new_source] = hfp_secondary_link;
    }
    else
    {
        // No need to make change, existing mapping is correct.
    }
}

void HfpProfile_UpdateMappingOnDisconnection(voice_source_t source_to_remove)
{
    /* B-303472 - secondary is no longer promoted on primary disconnection, so just ensure the correct source  is
     * set as invalid.
     */
    if (source_to_remove != voice_source_hfp_1 && source_to_remove != voice_source_hfp_2)
    {
        Panic();
    }
    else
    {
        hfp_link_priority link_to_remove = (source_to_remove == hfp_link_prio_mappings.link_prio_to_voice_source_map[hfp_primary_link]) ? hfp_primary_link : hfp_secondary_link;

        hfp_link_prio_mappings.voice_source_to_link_prio_map[source_to_remove] = hfp_invalid_link;
        hfp_link_prio_mappings.link_prio_to_voice_source_map[link_to_remove] = voice_source_none;
    }
}

void HfpProfile_InitHfpLinkPrioMapping(void)
{
    memset(&hfp_link_prio_mappings, 0, sizeof(hfp_link_prio_mapping_t));
}



