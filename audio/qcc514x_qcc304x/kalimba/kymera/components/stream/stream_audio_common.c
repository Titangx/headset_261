/****************************************************************************
 * Copyright (c) 2011 - 2017 Qualcomm Technologies International, Ltd.
****************************************************************************/
/**
 * \addtogroup Audio Audio endpoint
 * \ingroup endpoints
 * \file  stream_audio_common.c
 *
 * stream audio type file. <br>
 * This file contains stream functions for audio endpoints. <br>
 *
 * \section sec1 Contains:
 * stream_audio_get_endpoint <br>
 */

/****************************************************************************
Include Files
*/

#include "stream_private.h"
#include <limits.h>

/****************************************************************************
Private Type Declarations
*/

/****************************************************************************
Private Constant Declarations
*/

/****************************************************************************
Private Macro Declarations
*/
#define GET_CHANNEL_FROM_AUDIO_EP_KEY(key) \
        (((key) >> AUDIO_EP_CHANNEL_SHIFT) & AUDIO_EP_CHANNEL_MASK)

/****************************************************************************
Private Variable Definitions
*/

/****************************************************************************
Private Function Definitions
*/

/**
 * \brief Get the audio buffer length based on the minimum needed buffer size.
 *
 * \param buffer_size the minimum buffer size needed in words.
 *
 * \return length of the required buffer in words.
 */
static unsigned int get_audio_buffer_size(unsigned buffer_size)
{
    /* Pick the smallest power-of-2 buffer size, allowing about 10% extra overhead */
    if (buffer_size <= 115)
    {
        return 128;
    }
    else if (buffer_size <= 230)
    {
        return 256;
    }
    else if (buffer_size <= 460)
    {
        return 512;
    }
    else if (buffer_size <= 920)
    {
        return 1024;
    }
    else
    {
        return 2048;
    }
}

/****************************************************************************
Protected Function Definitions
*/
/****************************************************************************
 * create_stream_key
 */
unsigned create_stream_key(unsigned int hardware, unsigned int instance,
                                unsigned int channel)
{
    /* The key for audio encodes the hardware, instance and channel into 24
     * bits. Bits 3:0 are the channel, bits 7:4 the instance, bits 15:8 the
     * hardware type and bits 16:23 are always zero.
     */
    unsigned key;

    key = (hardware & AUDIO_EP_DEVICE_MASK) << AUDIO_EP_DEVICE_SHIFT;
    key |= ((instance & AUDIO_EP_INSTANCE_MASK) << AUDIO_EP_INSTANCE_SHIFT);
    key |= ((channel & AUDIO_EP_CHANNEL_MASK) << AUDIO_EP_CHANNEL_SHIFT);

    return key;
}

STREAM_DEVICE stream_get_device_type(ENDPOINT *ep)
{
    return (STREAM_DEVICE) GET_DEVICE_FROM_AUDIO_EP_KEY(ep->key);
}

/*
 * get_hardware_instance
 */
STREAM_INSTANCE get_hardware_instance(ENDPOINT *ep)
{
    return GET_INSTANCE_FROM_AUDIO_EP_KEY(ep->key);
}

/*
 * get_hardware_channel
 */
STREAM_CHANNEL get_hardware_channel(ENDPOINT *ep)
{
    return GET_CHANNEL_FROM_AUDIO_EP_KEY(ep->key);
}

/*
 * add_to_sync_list
 */
bool add_to_sync_list(ENDPOINT *ep1, ENDPOINT *ep2)
{
    ENDPOINT **p_ep;
    patch_fn_shared(stream_audio);

    /* There are a 2 scenarios where this call is supported
     * CASE-1: Neither ep1 nor ep2 is synchronised to anything else. Each ep has its own
     *         single slot cbops manager object. In this case, ep1 becomes the head of
     *         sync; a new multichannel cbops object is created with max number of
     *         supported channels & assigned to both endpoints.
     * CASE-2: ep1 is already part of a sync'd group but ep2 is not. EP1 and other eps
     *         in it's sync group share the same multichannel cbops. Ep2 has a single
     *         slot cbops manager object. In this case, ep1's head of sync becomes the
     *         head of sync for ep2; ep1's multichannel cbop is assigned to ep2.
     * CASE-3: ep2 is already part of a sync'd group but ep1 is not. This case is same as CASE-2
     *         so only swapping of ep2 and ep1 will do the expected job.
     * CASE-4: Both ep1 & ep2 are already part of a sync'd group (not the same one). In this case
     *         merging of multichannel sync groups will take place. All eps of EP2 sync group will get
     *         removed one by one and added to EP1 multichannel sync group.
     *
     * There is a basic assumption about the max number of channels (NR_MAX_SYNCED_EPS)
     * in cbops. This could be a build define, since the number of channels might need
     * to be more for chips like Crescendo. For cases with endpoints already part of
     * sync group, what will happen if the total endpoints in the sync group exceeds the
     * number of channels supported? If it has to fail, it should fail early.
     *
     * For scenarios (including todos above) where endpoints are already part of a sync
     * group, does it matter which is the head group? For example, if we swap ep1 & ep2
     * in TODO_1, then that is the same as CASE-2. However, "channel" number allocated
     * for each endpoint will be different.
     *
     * Finally, add_to_sync_list and remove_from_sync_list is duplicated word for word
     * in add_file_to_sync_list & remove_file_endpoint_from_sync_list respectively. The
     * only difference is that ep->state.file is used instead of ep->state.audio.
     *
     * The following scenarios are not supported nor checked.
     * NOT-1: ep1 & ep2 are the same.
     *          add_to_synchronisation() implementation checks for (ep1 == ep2)
     * NOT-2: ep1 & ep2 already belong to the same sync group.
     *          add_to_synchronisation() implementation checks for ALREADY_SYNCHRONISED()
     * NOT-3: ep2 is NULL.
     *          should call remove_from_sync_list() instead.
     */

      /* Check if EP2 is already synced and EP1 is NULL so we can append chain like EP2 -->EP1 */
    if(IS_AUDIO_ENDPOINT_SYNCED(ep2) == TRUE && IS_AUDIO_ENDPOINT_SYNCED(ep1)==FALSE)
    {
        ENDPOINT * l_tempEP;
        l_tempEP = ep1;
        ep1 = ep2;
        ep2 =l_tempEP;
    }
    /* iterate until the end - it's just a while. */
    for(p_ep = &ep1->state.audio.head_of_sync; *p_ep != NULL; p_ep = &((*p_ep)->state.audio.nep_in_sync));

    /*   - if first sync pair (first ever call to this with head plus some other ep) then
     *     create the multi-channel cbops for the "max" channels, put in first 2 eps' stuff,
     *     subsequent sync pairs will add their info as they arrive.
     *   - if connect() already happened for those endpoints, then buffer info already exists... and hook it in,
     *     otherwise not yet there, and gets hooked in later on when connect is done.
     *
     *     Remove existing individual single-channel cbops for one or both endpoints, depending on case above.
     */

    if(ep1->state.audio.head_of_sync->state.audio.nep_in_sync == NULL)
    {
        cbops_mgr* cbops;

        /* First sync pair, so ep1 is the head of a yet non-existent group. So create multi-channel cbops and clone flags from
         * ep1's cbops (latter has at least the flags set up, as endoint create caused cbops_mgr_create() already).
         */
        unsigned flags = cbops_get_flags(ep1->cbops);

        /* Create - later only NR_MAX_SYNCED_EPS or less channels can be actually in use. It could be generalised further,
         * with fully dynamic use of channel numbers - but quite some impact on table management when not yet needed... */
        if((cbops = cbops_mgr_create(ep1->direction, flags)) == NULL)
        {
            return FALSE;
        }

        /* Grab buffer etc. information from old ep1 and ep2 one-channel cbops. These may not have been connected yet!
         * So then the buffer info they have may still be void - but will later get set up if connect occurs later.
         * As this is first sync pair, the channels assigned to them are 0 and 1, simples... Get buffer info from "channel 0"
         * of the existing single-channel cbops chains.
         */
        tCbuffer *in_buffs[NR_MAX_SYNCED_EPS], *out_buffs[NR_MAX_SYNCED_EPS];

        /* Not yet known buffer info is clearly marked as absent */
        unsigned chan;
        for(chan = 2; chan < NR_MAX_SYNCED_EPS; chan++)
        {
            in_buffs[chan] = NULL;
            out_buffs[chan] = NULL;
        }

        /* First two channels' buffer info is hooked in from the single-channel cbops' information */
        cbops_mgr_get_buffer_info(ep1->cbops, 1, &in_buffs[0], &out_buffs[0]);
        cbops_mgr_get_buffer_info(ep2->cbops, 1, &in_buffs[1], &out_buffs[1]);

        /* The parameters come from head's so far single-channel cbops chain. Use that to set up the multi-channel cbops,
         * only first two channels' buffer information is present at this stage.
         */

        CBOP_VALS vals;
        /* If connect() didn't happen yet, then the chain is having no actual parameter values yet - so we can't generally
         * rely on params found inside cbops chain(s) owned by ep1 and ep2 at this point in time!
         * So on some platforms, and in general here, has to set proper parameters that come from the new head of the group.
         */
        stream_audio_set_cbops_param_vals(ep1, &vals);

        /*
         * The cbops_mgr_connect() will have to look at every buffer table entry and only have in-use channel indexes
         * for the channels where buffer info exists. For any NULL buffer ptr in table, the UNUSED channel index marker
         * must be set and this is what gets passed to the cbops to work with.
         * It just so happens that, for a while, endpoints can have simple channel indexing and in/out channel numbers match up.
         */
        if(!cbops_mgr_connect(cbops, NR_MAX_SYNCED_EPS, in_buffs, out_buffs, &vals))
        {
            cbops_mgr_destroy(cbops);
            return FALSE;
        }

        /* Remove old single-channel cbops from ep1 and ep2 */
        if((!cbops_mgr_destroy(ep1->cbops)) || (!cbops_mgr_destroy(ep2->cbops)))
        {
            cbops_mgr_destroy(cbops);
            return FALSE;
        }

        /* Replace old one-channel cbops with multichannel one in the head */
        ep1->cbops = cbops;
        ep2->cbops = cbops;

        /* First ever two endpoints in group have channels 0 and 1 - others may join */
        ep1->state.audio.channel = 0;
        ep2->state.audio.channel = 1;
    }
    /*  This belongs to CASE4 explained above.To merge two sync group into one,catch the head of ep2's sync group and
     *  remove each element of ep2 group and again add them into ep1 group. l_recursive_cnt is for tracking any infinite loop situation.
     */

    else if((IS_AUDIO_ENDPOINT_SYNCED(ep2)))
    {
        ep2 = ep2->state.audio.head_of_sync;
        int l_recursive_cnt = 0;
        while(ep2 != NULL)
        {
            ENDPOINT *l_temp = ep2;
            ep2 = ep2->state.audio.nep_in_sync;

            /* Remove l_temp from EP2 multichannel list. */
            if(remove_from_sync_list(l_temp) == FALSE)
            {
                return FALSE;
            }
            /* Add the l_temp to the end of the EP1's list. */
            if(add_to_sync_list(ep1,l_temp) == FALSE)
            {
                return FALSE;
            }
            l_recursive_cnt++;
            if(l_recursive_cnt >= NR_MAX_SYNCED_EPS)
            {
                return FALSE;
            }
        }
        return TRUE;
    }
    else
    {
        /* A sync group and its multi-channel cbops already exists
         * In the sync group, find first unoccupied channel - the endpoints that "joined" the group already had their
         * channel number allocated. Standalone endpoints have zero channel number, when these join the group, they get
         * assigned an unoccupied channel.
         * Sync group exists, and head endpoint has channel zero always - so we can look for smallest non-zero number
         * that is not yet allocated.
         */
        bool chan_nr[NR_MAX_SYNCED_EPS];
        unsigned chan;
        for(chan = 0; chan < NR_MAX_SYNCED_EPS; chan_nr[chan++] = FALSE);

        ENDPOINT *temp = ep1->state.audio.head_of_sync;

        while(temp != NULL)
        {
            chan_nr[temp->state.audio.channel] = TRUE;
            temp = temp->state.audio.nep_in_sync;
        }

        for(chan = 0; (chan < NR_MAX_SYNCED_EPS) && chan_nr[chan]; chan++);

        /* If added more than max number that can be in a sync group, fail */
        if(chan >= NR_MAX_SYNCED_EPS)
        {
            return FALSE;
        }

        ep2->state.audio.channel = chan;

        /* Get buffer info from the one and only channel in existing ep2 cbops chain (channel 0). Note that the buffer info
         * on some platforms and depending on order of operations may still be void.
         */
        tCbuffer *in_buff, *out_buff;
        cbops_mgr_get_buffer_info(ep2->cbops, 1, &in_buff, &out_buff);

        /* Plug that info to the "found" free channel in the multi-channel cbops. This may be superfluous at this point,
         * in case connect hasn't happened yet - well, on some platforms may come later, so actual information will be
         * hooked in at that point.
         */
        if (!cbops_mgr_connect_channel(ep1->state.audio.head_of_sync->cbops, chan, in_buff, out_buff))
        {
            return FALSE;
        }

        /* Destroy old single-channel cbops for ep2, replace ep2's cbops ptr with existing multi-channel cbops */
        cbops_mgr_destroy(ep2->cbops);
        ep2->cbops = ep1->state.audio.head_of_sync->cbops;
    }
    /* If the cbops contain a rateadjust then initialise it into passthrough mode */
    cbops_mgr_rateadjust_passthrough_mode(ep2->cbops, TRUE);

    /* Add the new element to the end of the list. */
    *p_ep = ep2;
    /* Set the head of the synch for ep2. */
    ep2->state.audio.head_of_sync = ep1->state.audio.head_of_sync;
    /* Two synchronised group can also be synchronised.
     * Because of this we keep ep->state.audio.nep_in_sync*/
    return TRUE;
}

/*
 * remove_from_sync_list
 */
bool remove_from_sync_list(ENDPOINT *ep)
{
    ENDPOINT **p_ep;
    ENDPOINT *new_head = ep->state.audio.head_of_sync;
    tCbuffer *in_buff, *out_buff;
    CBOP_VALS vals;
    patch_fn_shared(stream_audio);

    /* Here EP becomes a "standalone" ep so it needs a single-channel cbops chain...
     * So it needs to create a single-channel cbops chain with same buffer info and
     * params as the channel in the multi-channel cbops it is being removed from.
     */

    /* Get the current flags */
    unsigned flags = cbops_get_flags(ep->cbops);

    /* Create single-channel new cbops with flags & params cloned from multi-channel cbops */
    cbops_mgr* cbops;

    if((cbops = cbops_mgr_create(ep->direction, flags)) == NULL)
    {
        return FALSE;
    }

    /* Connect the new (single channel) cbops chain - so get the buffer info from multi-channel cbops for this
     * channel and plug it into the new cbops chain. Alternative is to set it based on here-and-now buffer ptrs inside
     * EP struct...
     */
    cbops_mgr_get_channel_buffers(ep->cbops, ep->state.audio.channel, &in_buff, &out_buff);

    /* The single-channel cbops has to rely on its own parameters,
     * instead of clones from the sync group's multichannel cbops that it is removed from.
     */
    stream_audio_set_cbops_param_vals(ep, &vals);

    /* Now "connect" all the info we got from multi-channel cbops in the new single-channel cbops. */
    if (!cbops_mgr_connect(cbops, 1, &in_buff, &out_buff, &vals))
    {
        cbops_mgr_destroy(cbops);
        return FALSE;
    }

    /* Anything removed from sync list has to mark itself as unused channel index and NULL the buffer ptr!
     * Any existing multi-channel cbops may stay in place until head is removed... and things can be "added back"
     * while it exists.
     * The multi-channel cbops is a reflection of a sync group, and while sync group of > 1 eps exists, the multi-chan
     * cbops chain exists.
     */

    cbops_mgr_set_unused_channel(ep->cbops, ep->state.audio.channel);

    if( IS_ENDPOINT_HEAD_OF_SYNC(ep) )
    {
        /* Removing the head of the list. */
        new_head = ep->state.audio.nep_in_sync;
    }

    //remove from list; and give new head if necessary
    for(p_ep = &new_head; *p_ep != NULL; p_ep = &((*p_ep)->state.audio.nep_in_sync))
    {
        /* update the new head */
        (*p_ep)->state.audio.head_of_sync = new_head;
        /* Remove ep1 only if it is not the head of list */
        if((*p_ep)->state.audio.nep_in_sync == ep)
        {
            (*p_ep)->state.audio.nep_in_sync = ep->state.audio.nep_in_sync;
        }
    }

    /* Now ep is "standalone" endpoint */
    ep->state.audio.head_of_sync = ep;
    ep->state.audio.nep_in_sync = NULL;

    /* Replace ep's cbops ptr with one pointing to the new single-channel cbops */
    ep->cbops = cbops;
    /* Initialise rateadjust to passthrough as haven't been asked to rateadjust yet */
    cbops_mgr_rateadjust_passthrough_mode(cbops, TRUE);

    return TRUE;
}

/*
 * sync_endpoints
 * ep1 is never null
 */
bool sync_endpoints (ENDPOINT *ep1, ENDPOINT *ep2)
{
    if (ep2 != NULL)
    {
        return add_to_synchronisation(ep1,ep2);
    }
    else
    {
        return remove_from_synchronisation(ep1);
    }
}

/**
 * \brief Determines whether two audio endpoints share a clock source.
 *
 * \param ep1 Endpoint to compare with ep2.
 * \param ep2 Endpoint to compare with ep1.
 * \param both_local boolean indicating if both endpoints are locally clocked.
 *
 * \return TRUE if ep1 and ep2 share a clock source, otherwise FALSE.
 */
bool stream_audio_have_same_clock(ENDPOINT *ep1, ENDPOINT *ep2, bool both_local)
{
    patch_fn_shared(stream_audio);

    if (both_local)
    {
        return TRUE;
    }
    /* At least one of the devices is a slave, if they are the same type of
     * hardware, and the same hardware instance than they must have the same
     * clock. */
    if (stream_get_device_type(ep1) == stream_get_device_type(ep2))
    {
        if (get_hardware_instance(ep1) == get_hardware_instance(ep2))
        {
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * stream_audio_have_sidetone_path_setup
 */
bool stream_audio_have_sidetone_path_setup(ENDPOINT *mic_ep, ENDPOINT *spkr_ep)
{
#ifdef INSTALL_AUDIO_MODULE
    patch_fn_shared(stream_audio_hydra);
    /* see if there is a sidetone path setup */
    return audio_vsm_sids_have_sidetone_path_setup(
        stream_external_id_from_endpoint(mic_ep),
        stream_external_id_from_endpoint(spkr_ep));
#else
    return FALSE;
#endif
}

/*
 * audio_configure_rm_enacting
 */
bool audio_configure_rm_enacting(ENDPOINT *endpoint, uint32 value)
{
    AUDIO_DATA_FORMAT format;

    /* If the data format isn't FIXP then we shouldn't be asked to
     * rateadjust */
    format = audio_get_data_format(endpoint);
    if (AUDIO_DATA_FORMAT_FIXP == format)
    {
        /* If we've been asked to enact then we want to disable passthrough mode
         * and vice versa. */
        bool passthrough = !((bool)value);
#ifdef INSTALL_DELEGATE_RATE_ADJUST_SUPPORT
        if(0 != endpoint->state.audio.external_rate_adjust_opid)
        {
            /* endpoint is using a standalone operator for rate adjustment,
             * set the built-in cbops rate adjust operator to passthrough mode,
             * for efficiency we also could remove it altogether.
             */
            if(!cbops_mgr_rateadjust_passthrough_mode(endpoint->cbops, TRUE))
            {
                return FALSE;
            }

            /* set the passthrough mode of the standalone operator */
            stream_delegate_rate_adjust_set_passthrough_mode(endpoint->state.audio.external_rate_adjust_opid,
                                                             passthrough);
            return TRUE;
        }
        else
#endif /* INSTALL_DELEGATE_RATE_ADJUST_SUPPORT */
        {
            return cbops_mgr_rateadjust_passthrough_mode(endpoint->cbops, passthrough);
        }
    }

    return FALSE;
}

/*
 * audio_get_config_rm_ability
 */
bool audio_get_config_rm_ability(ENDPOINT *endpoint, uint32 *value)
{
    AUDIO_DATA_FORMAT format = audio_get_data_format(endpoint);
    if (AUDIO_DATA_FORMAT_FIXP == format)
    {
        /* If it's a codec then it should be HW support */
        *value = (uint32)RATEMATCHING_SUPPORT_SW;
    }
    else
    {
        *value = (uint32)RATEMATCHING_SUPPORT_NONE;
    }
    return TRUE;
}

#ifdef TIMED_PLAYBACK_MODE
/*
 * audio_adjust_rm_rate
 *
 * This function gets passed to the timed playback module
 * and is called to adjust the output rate.
 *
 * It's currently only used when hardware warp is active,
 * although (with some refactoring) it could be used for SRA too.
 */
static void audio_adjust_rm_rate(void *data, int32 adjust_val)
{
    ENDPOINT *ep = (ENDPOINT *)data;
    ep->functions->configure(ep, EP_RATEMATCH_ADJUSTMENT, adjust_val);
}
#endif /* TIMED_PLAYBACK_MODE */


void stream_audio_kick(ENDPOINT *endpoint, ENDPOINT_KICK_DIRECTION kick_dir)
{
    ENDPOINT* synced;
    ENDPOINT_KICK_DIRECTION dir;
    endpoint_audio_state* ep_audio = &endpoint->state.audio;

#ifdef DEBUG_KICK_PROC_TIME
    TIME kick_start_time, kick_end_time;
#endif
    /* delta_samples may only be in use on some platforms
     * (e.g. Hydra does not use it)
     */
    ep_audio->delta_samples = stream_audio_get_rm_data(endpoint);

#ifdef DEBUG_KICK_PROC_TIME
    kick_start_time = time_get_time();
#endif

    stream_debug_buffer_levels(endpoint);

#ifdef TIMED_PLAYBACK_MODE
    /* If the first data have metadata tag then use the timed playback module. */
    if ((SINK == endpoint->direction) && (!ep_audio->data_flow_started))
    {
        unsigned samples = cbuffer_calc_amount_data_in_words(ep_audio->source_buf);
        metadata_tag *tag = buff_metadata_peek(ep_audio->source_buf);

        if( (samples>0)&&(tag != NULL)&&(IS_TIMESTAMPED_TAG(tag) || IS_VOID_TTP_TAG(tag)) )
        {
            /* Create the timed playback module. */
            if ((ep_audio->timed_playback = timed_playback_create()) == NULL)
            {
                panic_diatribe(PANIC_AUDIO_NOT_ENOUGH_MEMORY_FOR_TIMED_PLAYBACK, endpoint->id);
            }

            if (!timed_playback_init(ep_audio->timed_playback, endpoint->cbops,
                    audio_vsm_get_sample_rate_from_sid(stream_external_id_from_endpoint(endpoint)),
                    stream_if_get_system_kick_period(), endpoint->con_id, endpoint->id,
                    ep_audio->endpoint_delay_us) )
            {
                panic_diatribe(PANIC_AUDIO_TIMED_PLAYBACK_INIT_FAIL, endpoint->id);
            }
#ifdef INSTALL_DELEGATE_RATE_ADJUST_SUPPORT
            if (0 != endpoint->state.audio.external_rate_adjust_opid)
            {
                /* timed playback needs to do rate adjustment using standalone
                 * rate adjust operator.
                 */
                timed_playback_enable_delegate_rate_adjust(ep_audio->timed_playback,
                                                           endpoint->state.audio.external_rate_adjust_opid);
            } else
#endif
            if (ep_audio->rm_support == RATEMATCHING_SUPPORT_HW)
            {
                endpoint->functions->configure(endpoint, EP_RATEMATCH_ENACTING, TRUE);
                timed_playback_enable_hw_warp(ep_audio->timed_playback, (void *)endpoint, audio_adjust_rm_rate);
            }

            ep_audio->data_flow_started = TRUE;
            ep_audio->use_timed_playback = TRUE;
        }
    }
#endif
    if(endpoint->connected_to != NULL)
    {
        unsigned num_cbops_read = 0;
        unsigned num_cbops_written = 0;

#ifdef TIMED_PLAYBACK_MODE
        if ((SINK == endpoint->direction)
            && (ep_audio->timed_playback != NULL)
            && ep_audio->use_timed_playback)
        {
            timed_playback_run(ep_audio->timed_playback);
        }
        else
#endif /* TIMED_PLAYBACK_MODE */
        {
            unsigned max_copy = CBOPS_MAX_COPY_SIZE - 1;
            /* Execute the cbops chain, it will copy as much as it can into/out of the port,
             * and execute the necessary cbops_operation.
             */
#if defined(CHIP_BASE_HYDRA)
            unsigned before_rd_offset = cbuffer_get_read_mmu_offset(ep_audio->source_buf);
            unsigned before_wr_offset = cbuffer_get_write_mmu_offset(ep_audio->sink_buf);
#else /* not CHIP_BASE_HYDRA*/
            /* Looking at the amount of data before/after is less safe than
             * looking only at the offsets which are modified here. It is up
             * to stream_audio_process_rm_data() to know whether it can trust
             * the results.
             */
            unsigned before_rd_amount = cbuffer_calc_amount_data_in_words(ep_audio->source_buf);
            unsigned before_wr_amount = cbuffer_calc_amount_data_in_words(ep_audio->sink_buf);
#endif /* CHIP_BASE_HYDRA */

#ifdef INSTALL_METADATA
            if ((SINK == endpoint->direction) && buff_has_metadata(ep_audio->source_buf))
            {
                max_copy = buff_metadata_available_octets(ep_audio->source_buf) / OCTETS_PER_SAMPLE;
            }
#endif

            cbops_mgr_process_data(endpoint->cbops, max_copy);

#if defined(CHIP_BASE_HYDRA)
            unsigned after_rd_offset =  cbuffer_get_read_mmu_offset(ep_audio->source_buf);
            if (after_rd_offset >= before_rd_offset)
            {
                num_cbops_read = after_rd_offset - before_rd_offset;
            }
            else
            {
                unsigned buffsize_rd = cbuffer_get_size_in_words(ep_audio->source_buf);
                num_cbops_read = after_rd_offset + buffsize_rd - before_rd_offset;
            }

            unsigned after_wr_offset = cbuffer_get_write_mmu_offset(ep_audio->sink_buf);
            if (after_wr_offset >= before_wr_offset)
            {
                num_cbops_written = after_wr_offset - before_wr_offset;
            }
            else
            {
                unsigned buffsize_wr = cbuffer_get_size_in_words(ep_audio->sink_buf);
                num_cbops_written = after_wr_offset + buffsize_wr - before_wr_offset;
            }
#else /* not CHIP_BASE_HYDRA */
            unsigned after_rd_amount = cbuffer_calc_amount_data_in_words(ep_audio->source_buf);
            unsigned after_wr_amount = cbuffer_calc_amount_data_in_words(ep_audio->sink_buf);
            if (after_rd_amount < before_rd_amount)
            {
                num_cbops_read = before_rd_amount - after_rd_amount;
            }
            if (after_wr_amount > before_wr_amount)
            {
                num_cbops_written = after_wr_amount - before_wr_amount;
            }
#endif /* CHIP_BASE_HYDRA */

#ifdef INSTALL_METADATA
            if ((SINK == endpoint->direction) && buff_has_metadata(ep_audio->source_buf))
            {
                /* Discard any metadata from the source buffer */
                unsigned b4idx, afteridx;
                buff_metadata_tag_list_delete(
                        buff_metadata_remove(ep_audio->source_buf,
                                             OCTETS_PER_SAMPLE * num_cbops_read,
                                             &b4idx, &afteridx));
            }
#endif /* INSTALL_METADATA */
        }

        stream_audio_process_rm_data(endpoint, num_cbops_read, num_cbops_written);
    }

    /* Work out which way this sync group kicks (if it is a sync group) */
    if (SOURCE == endpoint->direction)
    {
        dir = STREAM_KICK_FORWARDS;
    }
    else
    {
        dir = STREAM_KICK_BACKWARDS;
    }

    for (synced = endpoint; synced != NULL; synced = synced->state.audio.nep_in_sync)
    {
        /* The endpoint's work is finished. We are the end of the
         * chain and then the kicks will go backwards.
         * This mechanism is implemented in stream_if_propagate_kick.
         *
         * Propagates_kicks isn't set if the endpoint isn't connected so no
         * need to check this here.*/
        propagate_kick(synced, dir);

        stream_debug_buffer_levels(synced);
    }

#ifdef DEBUG_KICK_PROC_TIME
        kick_end_time = time_get_time();
        if (time_sub(kick_end_time, kick_start_time) > 10*MILLISECOND)
        {
            panic_diatribe(PANIC_AUDIO_PROCESSING_OVERRUN, (DIATRIBE_TYPE)((uintptr_t)endpoint));
        }
#endif

}

#ifdef HAVE_CONTINUOUS_BUFFERING
/*
 * stream_audio_get_hw_buff_size
 */
unsigned int stream_audio_get_hw_buff_size(unsigned buffer_size)
{
    return get_audio_buffer_size(buffer_size);
}
#endif /* HAVE_CONTINUOUS_BUFFERING */

/*
 * stream_audio_get_buffer_length
 */
unsigned int stream_audio_get_buffer_length(uint32 sample_rate, ENDPOINT_DIRECTION dir, bool get_hw_size)
{
    unsigned buffer_size;
    TIME_INTERVAL kick_period;
    patch_fn_shared(stream_audio);
#ifdef TODO_MITIGATE_CBOPS_DRIFT_IN_NON_TTP_USE_CASE
    /* This is to make sure when WBS is used the output buffer size will be
     * 256 samples instead of 128 samples
     */
    if((SINK == dir)
       && (sample_rate == 16000))
    {
        return 256;
    }
#else
    NOT_USED(dir);
#endif
    /* Calculate required buffer size based on kick period and sample rate */
    /* Max possible supported sample rate is 192kHz,
     * which allows up to about 22ms before this would overflow
     */
    kick_period = stream_if_get_system_kick_period();
    PL_ASSERT(kick_period <= 20000);

    buffer_size = 2 * (unsigned)(1 + (sample_rate *  kick_period)/1000000);

    if (get_hw_size)
    {
        return get_audio_buffer_size(buffer_size);
    }
    else
    {
        return buffer_size;
    }
}

#ifdef INSTALL_AUDIO_MODULE
void stream_audio_post_create_callback(unsigned sid, bool success)
{
    ENDPOINT *endpoint = stream_endpoint_from_extern_id(sid);
    patch_fn_shared(stream_audio_hydra);

    if (success)
    {
        /* Hardware for endpoint successfully allocated. */
        if(endpoint->stream_endpoint_type == endpoint_audio)
        {
            endpoint->state.audio.hw_allocated = TRUE;
        }
#ifdef INSTALL_SPDIF
        else if(endpoint->stream_endpoint_type == endpoint_spdif)
        {
            endpoint->state.spdif.hw_allocated = TRUE;
        }
#endif /* INSTALL_SPDIF */
    }
    stream_if_ep_creation_complete(endpoint, success);
    return;
}

bool stream_audio_post_create_check(unsigned int hardware,
                                    unsigned int instance,
                                    unsigned int channel,
                                    ENDPOINT_DIRECTION dir,
                                    ENDPOINT *endpoint,
                                    bool *pending)
{
    return audio_vsm_obtain_hardware((audio_hardware)hardware,
                            (audio_instance)instance, (audio_channel)channel,
                            (dir == SINK ? TRUE : FALSE),
                            ((SID) stream_external_id_from_endpoint(endpoint)),
                            pending, stream_audio_post_create_callback);
}
#endif /* INSTALL_AUDIO_MODULE */

#ifdef INSTALL_UNINTERRUPTABLE_ANC
/*
 * stream_audio_get_anc_instance_id
 */
STREAM_ANC_INSTANCE stream_audio_anc_get_instance_id(ENDPOINT *ep)
{
    if (ep->stream_endpoint_type == endpoint_audio)
    {
        return (ep->state.audio.anc.instance_id);
    }
    else
    {
        return STREAM_ANC_INSTANCE_NONE_ID;
    }
}

/*
 * stream_audio_set_anc_instance_id
 */
void stream_audio_anc_set_instance_id(ENDPOINT *ep, STREAM_ANC_INSTANCE instance_id)
{
    if (ep->stream_endpoint_type == endpoint_audio)
    {
        ep->state.audio.anc.instance_id = instance_id;
    }
}

/*
 * stream_audio_get_anc_input_path_id
 */
STREAM_ANC_PATH stream_audio_anc_get_input_path_id(ENDPOINT *ep)
{
    if (ep->stream_endpoint_type == endpoint_audio)
    {
        return (ep->state.audio.anc.input_path_id);
    }
    else
    {
        return STREAM_ANC_PATH_NONE_ID;
    }
}

/*
 * stream_audio_set_anc_input_path_id
 */
void stream_audio_anc_set_input_path_id(ENDPOINT *ep, STREAM_ANC_PATH path_id)
{
    if (ep->stream_endpoint_type == endpoint_audio)
    {
        ep->state.audio.anc.input_path_id = path_id;
    }
}

#ifdef INSTALL_ANC_STICKY_ENDPOINTS
/*
 * stream_audio_get_anc_close_pending
 */
bool stream_audio_anc_get_close_pending(ENDPOINT *ep)
{
    if (ep->stream_endpoint_type == endpoint_audio)
    {
        return (ep->state.audio.anc.close_pending);
    }
    else
    {
        return FALSE;
    }
}

/*
 * stream_audio_set_anc_close_pending
 */
void stream_audio_anc_set_close_pending(ENDPOINT *ep, bool close_pending)
{
    if (ep->stream_endpoint_type == endpoint_audio)
    {
        ep->state.audio.anc.close_pending = close_pending;
    }
}
#endif /* INSTALL_ANC_STICKY_ENDPOINTS */

#endif /* INSTALL_UNINTERRUPTABLE_ANC */
