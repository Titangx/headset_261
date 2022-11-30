/*!
\copyright  Copyright (c) 2019-2020 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\file
\brief      HFP library link priority to Voice Source mappings. These are required
            to allow the HFP Profile CAA code to be able to associate messages
            received from the HFP library with HFP profile instances stored in the
            Device List (transitively, using a lookup of the Voice Source associated
            with the HFP instance).
*/

#ifndef HFP_PROFILE_VOICE_SOURCE_LINK_PRIO_MAPPING_H_
#define HFP_PROFILE_VOICE_SOURCE_LINK_PRIO_MAPPING_H_

#include <hfp.h>

#include "hfp_profile_typedef.h"

#include "voice_sources_list.h"

extern hfp_link_prio_mapping_t hfp_link_prio_mappings;

/*! \brief Accessor to get the HFP library link priority associated with a given Voice Source.
    \param source - the Voice Source for which to get the HFP link priority
    \return the HFP link priority associated with the voice source
*/
hfp_link_priority HfpProfile_GetHfpLinkPrioForVoiceSource(voice_source_t source);

/*! \brief Accessor to get the Voice Source associated with a given HFP library link priority.
    \param priority - the HFP link priority for which to get the Voice Source
    \return the Voice Source assigned the specified link priority
*/
voice_source_t HfpProfile_GetVoiceSourceForHfpLinkPrio(hfp_link_priority priority);

/*! \brief Update the HFP library link priority mappings when a Voice Source is connected.
    \param  new_source - the voice source that was connected

    This function updates the associations between Voice Sources and HFP library link priorities
    when a new HFP instance becomes connected.

    The first connection shall assume the hfp_primary_link, subsequent connections shall be
    secondary if a primary is already present, otherwise they will be primary.
*/
void HfpProfile_UpdateMappingOnConnection(voice_source_t new_source);

/*! \brief  Update the HFP library link priority mappings when a Voice Source is disconnected.
    \param  source_to_remove - the voice source that was disconnected

    When an HFP link becomes disconnected, the Voice Source associated with that instance of
    the HFP profile shall be deallocated. This function updates the HFP link priority associated
    with the disconnected Voice Source, which shall become set to hfp_invalid_link.

    Furthermore, if the HFP link priority associated with the disconnected Voice Source was
    hfp_link_primary, then the secondary link shall become promoted to the primary link.

    This function therefore handles the mapping associations as required for all the link
    priorities and Voice Sources when a voice source is disconnected.
*/
void HfpProfile_UpdateMappingOnDisconnection(voice_source_t source_to_remove);

/*! \brief Reset the HFP link priority mappings to default values. */
void HfpProfile_InitHfpLinkPrioMapping(void);

#endif /* HFP_PROFILE_VOICE_SOURCE_LINK_PRIO_MAPPING_H_ */
