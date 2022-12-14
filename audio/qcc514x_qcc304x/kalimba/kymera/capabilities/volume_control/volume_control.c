/**
 * Copyright (c) 2014 - 2017 Qualcomm Technologies International, Ltd.
 * \file  volume_control.c
 * \ingroup  capabilities
 *
 *  Volume Control
 *
 */

/****************************************************************************
Include Files
 */
#include "capabilities.h"
#include "volume_control_cap.h"
#include "vol_ctrl_config.h"
#include "common_conversions.h"


/****************************************************************************
Private Constant Definitions
 */

#define VOL_CTRL_BLOCK_SIZE      1
#define VOL_CTRL_BUFFER_SIZE     128
/* Special provision for the output block size of
 * an 1:6 upsampling IIR resampler (i.e. ringtone
 * at 8k, max output sample rate 48k)
*/
#define VOL_CTRL_BUFFER_SIZE_PROVISION  6

#define ZEROdBinQ5  1<<(DAWTH-5)

#ifdef CAPABILITY_DOWNLOAD_BUILD
#define _VOL_CTRL_VOL_CAP_ID CAP_ID_DOWNLOAD_VOL_CTRL_VOL
#else
#define _VOL_CTRL_VOL_CAP_ID CAP_ID_VOL_CTRL_VOL
#endif

#define VOL_CTRL_CHANNEL_MASK ((1<<VOL_CTRL_CONSTANT_NUM_CHANNELS)-1)

typedef enum {
    VOL_CTRL_KICK_WAIT_ON_DATA,
    VOL_CTRL_KICK_WAIT_ON_SPACE,
    VOL_CTRL_KICK_WAIT_ON_DATA_LATE,
    VOL_CTRL_KICK_WAIT_ON_SPACE_LATE,
    VOL_CTRL_KICK_AUX_PENDING,
    VOL_CTRL_KICK_STILL_AUX_PENDING
} VOL_CTRL_KICK_REASON;

/****************************************************************************
Private Function Definitions
 */

/* Local functions */
static bool vol_ctrl_fixup_buffer_details(VOL_CTRL_DATA_T *op_extra_data, unsigned terminal_id, OP_BUF_DETAILS_RSP *resp);
static void vol_ctrl_recalc_main_buffer_size(VOL_CTRL_DATA_T *op_extra_data, unsigned terminal_id);
static void vol_ctrl_recalc_aux_buffer_size(VOL_CTRL_DATA_T *op_extra_data);
static void vol_ctrl_kick_waiting(VOL_CTRL_DATA_T *op_extra_data, TOUCHED_TERMINALS *touched, VOL_CTRL_KICK_REASON reason);
#ifdef INSTALL_METADATA
static metadata_tag* vol_ctrl_handle_aux_metadata(VOL_CTRL_DATA_T *op_extra_data);
static void vol_ctrl_handle_input_metadata(VOL_CTRL_DATA_T *op_extra_data, metadata_tag* eoftag);
#endif
#ifdef VOLUME_CONTROL_AUX_TTP_SUPPORT
static void vol_ctrl_update_current_timestamp(VOL_CTRL_DATA_T *op_extra_data);
static void vol_ctrl_timestamp_void_tags(VOL_CTRL_DATA_T *op_extra_data, metadata_tag *mtag, unsigned octets_before_tag);
#endif

/** The volume control capability function handler table */
const handler_lookup_struct vol_ctlr_handler_table =
{
    vol_ctlr_create,          /* OPCMD_CREATE */
    vol_ctlr_destroy,         /* OPCMD_DESTROY */
    vol_ctlr_start,           /* OPCMD_START */
    base_op_stop,             /* OPCMD_STOP */
    base_op_reset,            /* OPCMD_RESET */
    vol_ctlr_connect,         /* OPCMD_CONNECT */
    vol_ctlr_disconnect,      /* OPCMD_DISCONNECT */
    vol_ctlr_buffer_details,  /* OPCMD_BUFFER_DETAILS */
    base_op_get_data_format,  /* OPCMD_DATA_FORMAT */
    vol_ctlr_get_sched_info   /* OPCMD_GET_SCHED_INFO */
};

/* Null terminated operator message handler table */
const opmsg_handler_lookup_table_entry vol_ctlr_opmsg_handler_table[] =
{{OPMSG_COMMON_ID_GET_CAPABILITY_VERSION,                base_op_opmsg_get_capability_version},

        {OPMSG_COMMON_ID_SET_CONTROL,                       vol_ctlr_opmsg_obpm_set_control},
        {OPMSG_COMMON_ID_GET_PARAMS,                        vol_ctlr_opmsg_obpm_get_params},
        {OPMSG_COMMON_ID_GET_DEFAULTS,                      vol_ctlr_opmsg_obpm_get_defaults},
        {OPMSG_COMMON_ID_SET_PARAMS,                        vol_ctlr_opmsg_obpm_set_params},
        {OPMSG_COMMON_ID_GET_STATUS,                        vol_ctlr_opmsg_obpm_get_status},

        {OPMSG_COMMON_ID_SET_UCID,                          vol_ctlr_opmsg_set_ucid},
        {OPMSG_COMMON_ID_GET_LOGICAL_PS_ID,                 vol_ctlr_opmsg_get_ps_id},

        {OPMSG_COMMON_SET_SAMPLE_RATE,                      vol_ctlr_opmsg_set_sample_rate},
        {OPMSG_COMMON_SET_DATA_STREAM_BASED,                vol_ctlr_opmsg_data_stream_based},
#ifdef VOLUME_CONTROL_AUX_TTP_SUPPORT
        {OPMSG_VOL_CTRL_ID_SET_AUX_TIME_TO_PLAY,            vol_ctlr_opmsg_set_aux_time_to_play},
        {OPMSG_VOL_CTRL_ID_SET_DOWNSTREAM_LATENCY_EST,      vol_ctlr_opmsg_set_downstream_latency_est},
#endif
        {0, NULL}};

/* Capability descriptor */
const CAPABILITY_DATA vol_ctlr_cap_data =
{
        _VOL_CTRL_VOL_CAP_ID,             /* Capability ID */
        VOL_CTRL_VOL_V2PLUS_VERSION_MAJOR, VOL_CTRL_CAP_VERSION_MINOR, /* Version information - hi and lo parts */
        2*VOL_CTRL_CONSTANT_NUM_CHANNELS,VOL_CTRL_CONSTANT_NUM_CHANNELS,  /* Max number of sinks/inputs and sources/outputs */
        &vol_ctlr_handler_table,      /* Pointer to message handler function table */
        vol_ctlr_opmsg_handler_table, /* Pointer to operator message handler function table */
        vol_ctlr_process_data,        /* Pointer to data processing function */
        0,                               /* Reserved */
        sizeof(VOL_CTRL_DATA_T)      /* Size of capability-specific per-instance data */
};
#if !defined(CAPABILITY_DOWNLOAD_BUILD)
MAP_INSTANCE_DATA(CAP_ID_VOL_CTRL_VOL, VOL_CTRL_DATA_T)
#else
MAP_INSTANCE_DATA(CAP_ID_DOWNLOAD_VOL_CTRL_VOL, VOL_CTRL_DATA_T)
#endif /* CAPABILITY_DOWNLOAD_BUILD */

/****************************************************************************
Private Function Definitions
*/
static inline VOL_CTRL_DATA_T *get_instance_data(OPERATOR_DATA *op_data)
{
    return (VOL_CTRL_DATA_T *) base_op_get_instance_data(op_data);
}

/****************************************************************************
Public Function Declarations
 */


/* ********************************** API functions ************************************* */

bool vol_ctlr_create(OPERATOR_DATA *op_data, void *message_data, unsigned *response_id, void **response_data)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);

    patch_fn_shared(volume_control_wrapper);

    if (!base_op_create(op_data, message_data, response_id, response_data))
    {
        return FALSE;
    }

    /*allocate the volume control shared memory*/
    op_extra_data->shared_volume_ptr = allocate_shared_volume_cntrl();
    if(!op_extra_data->shared_volume_ptr)
    {
        base_op_change_response_status(response_data, STATUS_CMD_FAILED);
        return TRUE;
    }

    op_extra_data->lpvols = &op_extra_data->host_vol;
    op_extra_data->stream_based = FALSE;

    /* Initialize mute control */
    op_extra_data->mute_period = 10;

    /* Initialize extended data for operator.  Assume initialized to zero*/
    op_extra_data->ReInitFlag = 1;

    op_extra_data->pending_timer = TIMER_ID_INVALID;

    if(!cpsInitParameters(&op_extra_data->parms_def,(unsigned*)VOL_CTRL_GetDefaults(base_op_get_cap_id(op_data)),(unsigned*)&op_extra_data->parameters,sizeof(VOL_CTRL_PARAMETERS)))
    {
        base_op_change_response_status(response_data, STATUS_CMD_FAILED);
        return TRUE;
    }

    return TRUE;
}



bool vol_ctlr_destroy(OPERATOR_DATA *op_data, void *message_data, unsigned *response_id, void **response_data)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);

    if (opmgr_op_is_running(op_data))
    {
        /* We can't destroy a running operator. */
        return base_op_build_std_response_ex(op_data, STATUS_CMD_FAILED, response_data);
    }

    /* call base_op destroy that creates and fills response message, too */
    if(!base_op_destroy(op_data, message_data, response_id, response_data))
    {
        return(FALSE);
    }

    /*free volume control shared memory*/
    release_shared_volume_cntrl(op_extra_data->shared_volume_ptr);
    op_extra_data->shared_volume_ptr = NULL;

    /* Release Channels */
    destroy_processing(op_extra_data);

    return TRUE;
}

bool vol_ctlr_get_sched_info(OPERATOR_DATA *op_data, void *message_data, unsigned *response_id, void **response_data)
{
    OP_SCHED_INFO_RSP* resp;

    resp = base_op_get_sched_info_ex(op_data, message_data, response_id);
    if (resp == NULL)
    {
        return base_op_build_std_response_ex(op_data, STATUS_CMD_FAILED, response_data);
    }
    *response_data = resp;

    resp->block_size = VOL_CTRL_BLOCK_SIZE;

    return TRUE;
}

/**
 * \brief Placeholder for fixups
 * \param terminal_id Allow any implementation to tell what changed
 * \param resp The response to update
 */
static bool vol_ctrl_fixup_buffer_details(VOL_CTRL_DATA_T *op_extra_data, unsigned terminal_id, OP_BUF_DETAILS_RSP *resp)
{
    patch_fn_shared(volume_control_wrapper);

    return TRUE;
}


bool vol_ctlr_buffer_details(OPERATOR_DATA *op_data, void *message_data, unsigned *response_id, void **response_data)
{
    VOL_CTRL_DATA_T* opx_data = get_instance_data(op_data);
    OP_BUF_DETAILS_RSP *resp;
    unsigned buffer_size, base_buffer_size;
    unsigned terminal_id = OPMGR_GET_OP_BUF_DETAILS_TERMINAL_ID(message_data);
#if !defined(DISABLE_IN_PLACE) && !defined(VC_NOT_RUN_IN_PLACE)
    unsigned term_idx;
#endif /* !defined(DISABLE_IN_PLACE) && !defined(VC_NOT_RUN_IN_PLACE) */

    if (!base_op_buffer_details(op_data, message_data, response_id, response_data))
    {
        return FALSE;
    }

    resp = (OP_BUF_DETAILS_RSP*)*response_data;
    base_buffer_size = resp->b.buffer_size + VOL_CTRL_BUFFER_SIZE_PROVISION;

#ifdef INSTALL_METADATA
    {
        /* If an input/output connection is already present and has metadata then
         * we are obliged to return that buffer so that metadata can be shared
         * between channels. */
        tCbuffer *meta_buff;

        if ((terminal_id & TERMINAL_SINK_MASK) == TERMINAL_SINK_MASK)
        {
            /* Only the main input channels consume metadata, the aux inputs do not */
            if ((terminal_id & 0x1) == 0)
            {
                meta_buff = opx_data->metadata_ip_buffer;
                resp->supports_metadata = TRUE;
            }
            else
            {
                if(NULL != opx_data->metadata_aux_channel)
                {
                    meta_buff = opx_data->metadata_aux_channel->buffer;
                }
                else
                {
                    meta_buff = NULL;
                }
                resp->supports_metadata = TRUE;
            }
        }
        else
        {
            meta_buff = opx_data->metadata_op_buffer;
            resp->supports_metadata = TRUE;
        }

        resp->metadata_buffer = meta_buff;

    }
#endif /* INSTALL_METADATA */

    /* buffer size of 2.5ms worth at the sample rate of the operator */
    buffer_size = frac_mult(opx_data->sample_rate,FRACTIONAL(0.0025));

    if (buffer_size==0)
    {
        buffer_size = VOL_CTRL_BUFFER_SIZE;
    }

    if (buffer_size < base_buffer_size)
    {
        buffer_size = base_buffer_size;
    }

#if !defined(DISABLE_IN_PLACE) && !defined(VC_NOT_RUN_IN_PLACE)
    if(terminal_id&TERMINAL_SINK_MASK)
    {
        if(terminal_id&0x1)
        {
            unsigned aux_min_size, main_buff_size = 0;

            /* Aux input : If we have a main input connected, 
             * try to make sure the aux buffer is at least as big
             */
            if (opx_data->metadata_ip_buffer != NULL)
            {
                main_buff_size = cbuffer_get_size_in_words(opx_data->metadata_ip_buffer);
            }

            if (buffer_size < main_buff_size)
            {
                buffer_size = main_buff_size;
            }

            /* Aux buffer should be at least 2 kick periods at the system sample rate */
            aux_min_size = 2 * (uint32)(((uint64)stream_if_get_system_kick_period() 
                * opx_data->sample_rate) / SECOND);
            
            if (buffer_size < aux_min_size)
            {
                buffer_size = aux_min_size;
            }

            /* Don't run in place on the aux inputs. */
            resp->runs_in_place = FALSE;

            /* Set the asked buffer size. */
            resp->b.buffer_size = buffer_size;
            opx_data->aux_buff_size = buffer_size;

            return vol_ctrl_fixup_buffer_details(opx_data, terminal_id, resp);
        }
        else
        {
            term_idx = (terminal_id&TERMINAL_NUM_MASK)>>1;

            /*input terminal. give the output buffer for the channel */
            resp->b.in_place_buff_params.buffer = opx_data->output_buffer[term_idx] ;

            /* Choose terminal associated with the term_idx. */
            resp->b.in_place_buff_params.in_place_terminal = term_idx;
        }
    }
    else
    {
        /* The output terminal index is directly mapped to the input_buffer array */
        unsigned buffer_idx = (terminal_id&TERMINAL_NUM_MASK);
        /* The input terminal index is the buffer index multiplied by 2. */
        term_idx = buffer_idx << 1;

        /*output terminal. give the input buffer for the channel */
        resp->b.in_place_buff_params.buffer = opx_data->input_buffer[buffer_idx];

        /* Choose terminal associated with the term_idx. */
        resp->b.in_place_buff_params.in_place_terminal = term_idx | TERMINAL_SINK_MASK;
    }

    /* Run in place*/
    resp->runs_in_place = TRUE;

    /* Set the asked buffer size. */
    resp->b.in_place_buff_params.size = buffer_size;


#else  /* !defined(DISABLE_IN_PLACE) && !defined(VC_NOT_RUN_IN_PLACE) */
    resp->b.buffer_size = buffer_size;
#endif /* !defined(DISABLE_IN_PLACE) && !defined(VC_NOT_RUN_IN_PLACE) */

    return vol_ctrl_fixup_buffer_details(opx_data, terminal_id, resp);
}

/**
 * \brief Placeholder for fixups
 * \param terminal_id Allow any implementation to tell what changed
 */
static void vol_ctrl_recalc_main_buffer_size(VOL_CTRL_DATA_T *op_extra_data, unsigned terminal_id)
{
    patch_fn_shared(volume_control_wrapper);
}

/**
 * \brief Update aux_buffer_size with the minimum of connected aux buffers' sizes
 */
static void vol_ctrl_recalc_aux_buffer_size(VOL_CTRL_DATA_T *op_extra_data)
{
    unsigned ch;
    unsigned aux_buffer_min_size = MAXINT;

    patch_fn_shared(volume_control_wrapper);

    for (ch = 0; ch < VOL_CTRL_CONSTANT_NUM_CHANNELS; ch += 1)
    {
        tCbuffer* aux_buffer = op_extra_data->aux_channel[ch].buffer;
        if (aux_buffer != NULL)
        {
            unsigned size = cbuffer_get_size_in_words(aux_buffer);
            aux_buffer_min_size = MIN(aux_buffer_min_size, size);
        }
    }
    op_extra_data->aux_buff_size = aux_buffer_min_size;
}

bool vol_ctlr_connect(OPERATOR_DATA *op_data, void *message_data, unsigned *response_id, void **response_data)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);
    unsigned terminal_id = OPMGR_GET_OP_CONNECT_TERMINAL_ID(message_data);    /* extract the terminal_id */
    tCbuffer* pterminal_buf = OPMGR_GET_OP_CONNECT_BUFFER(message_data);

    /* Setup Response to Connection Request. */
    if (!base_op_build_std_response_ex(op_data, STATUS_OK, response_data))
    {
        return FALSE;
    }

    /* (i)  check if the terminal ID is valid . The number has to be less than the maximum number of sinks or sources .  */
    /* (ii) check if we are connecting to the right type . It has to be a buffer pointer and not endpoint connection */
    if( !base_op_is_terminal_valid(op_data, terminal_id) || !pterminal_buf)
    {
        base_op_change_response_status(response_data, STATUS_CMD_FAILED);
        return TRUE;
    }

    if (opmgr_op_is_running(op_data))
    {
        /* Only Auxiliary Terminals Can be Altered while running */
        if(!(terminal_id & TERMINAL_SINK_MASK) || !(terminal_id&0x1))
        {
            base_op_change_response_status(response_data, STATUS_CMD_FAILED);
            return TRUE;
        }
    }

    if(terminal_id&TERMINAL_SINK_MASK)
    {
        unsigned term_idx = (terminal_id&TERMINAL_NUM_MASK)>>1;

        if(terminal_id&0x1)
        {
            opmgr_op_suspend_processing(op_data);
            op_extra_data->aux_connected |= (1<<term_idx);
            op_extra_data->aux_channel[term_idx].buffer = pterminal_buf;
            opmgr_op_resume_processing(op_data);
#ifdef INSTALL_METADATA
            if(op_extra_data->metadata_aux_channel == NULL)
            {
                if (buff_has_metadata(op_extra_data->aux_channel[term_idx].buffer))
                {
                    op_extra_data->metadata_aux_channel = &op_extra_data->aux_channel[term_idx];
                }
            }
#endif /* INSTALL_METADATA */
            vol_ctrl_recalc_aux_buffer_size(op_extra_data);
        }
        else
        {
            op_extra_data->input_buffer[term_idx] = pterminal_buf;
#ifdef INSTALL_METADATA
            if(op_extra_data->metadata_ip_buffer == NULL)
            {
                if (buff_has_metadata(op_extra_data->input_buffer[term_idx]))
                {
                    op_extra_data->metadata_ip_buffer = op_extra_data->input_buffer[term_idx];
                }
            }
#endif /* INSTALL_METADATA */
            vol_ctrl_recalc_main_buffer_size(op_extra_data, terminal_id);
        }
    }
    else
    {
        unsigned term_idx = terminal_id&TERMINAL_NUM_MASK;
        op_extra_data->output_buffer[term_idx] = pterminal_buf;
#ifdef INSTALL_METADATA
        if(op_extra_data->metadata_op_buffer == NULL)
        {
            if (buff_has_metadata(op_extra_data->output_buffer[term_idx]))
            {
                op_extra_data->metadata_op_buffer = op_extra_data->output_buffer[term_idx];
            }
        }
#endif /* INSTALL_METADATA */
        vol_ctrl_recalc_main_buffer_size(op_extra_data, terminal_id);
    }
    return TRUE;
}

bool vol_ctlr_disconnect(OPERATOR_DATA *op_data, void *message_data, unsigned *response_id, void **response_data)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);
    unsigned terminal_id = OPMGR_GET_OP_DISCONNECT_TERMINAL_ID(message_data);

    /* Setup Response to Disconnection Request. */
    if (!base_op_build_std_response_ex(op_data, STATUS_OK, response_data))
    {
        return FALSE;
    }

    /* (i)  check if the terminal ID is valid . The number has to be less than the maximum number of sinks or sources .  */
    /* (ii) check if we are connecting to the right type . It has to be a buffer pointer and not endpoint connection */
    if( !base_op_is_terminal_valid(op_data, terminal_id))
    {
        base_op_change_response_status(response_data, STATUS_CMD_FAILED);
        return TRUE;
    }

    if (opmgr_op_is_running(op_data))
    {
        /* Only Auxiliary Terminals Can be Altered while running */
        if(!(terminal_id & TERMINAL_SINK_MASK) || !(terminal_id&0x1))
        {
            base_op_change_response_status(response_data, STATUS_CMD_FAILED);
            return TRUE;
        }
    }

    if(terminal_id&TERMINAL_SINK_MASK)
    {
        unsigned term_idx = (terminal_id&TERMINAL_NUM_MASK)>>1;

        if(terminal_id&0x1)
        {
#ifdef INSTALL_METADATA
            if (op_extra_data->metadata_aux_channel == &op_extra_data->aux_channel[term_idx])
            {
                unsigned i;
                bool found_alternative = FALSE;
                vol_ctrl_aux_channel_t *aux_chan = NULL;
                for (i = 0, aux_chan = &op_extra_data->aux_channel[0];
                     i < VOL_CTRL_CONSTANT_NUM_CHANNELS;
                     i++, aux_chan++)
                {
                    if (i == term_idx)
                    {
                        continue;
                    }
                    if (aux_chan->buffer != NULL && buff_has_metadata(aux_chan->buffer))
                    {
                        op_extra_data->metadata_aux_channel = aux_chan;
                        found_alternative = TRUE;
                        break;
                    }
                }
                if (!found_alternative)
                {
                    op_extra_data->metadata_aux_channel = NULL;
                }
            }
#endif /* INSTALL_METADATA */
            opmgr_op_suspend_processing(op_data);
            op_extra_data->aux_connected &= ~(1<<term_idx);
            op_extra_data->aux_channel[term_idx].buffer = NULL;
            opmgr_op_resume_processing(op_data);
            vol_ctrl_recalc_aux_buffer_size(op_extra_data);
        }
        else
        {
#ifdef INSTALL_METADATA
            if (op_extra_data->metadata_ip_buffer == op_extra_data->input_buffer[term_idx])
            {
                unsigned i;
                bool found_alternative = FALSE;
                for (i = 0; i < VOL_CTRL_CONSTANT_NUM_CHANNELS; i++)
                {
                    if (i == term_idx)
                    {
                        continue;
                    }
                    if (op_extra_data->input_buffer[i] != NULL && buff_has_metadata(op_extra_data->input_buffer[i]))
                    {
                        op_extra_data->metadata_ip_buffer = op_extra_data->input_buffer[i];
                        found_alternative = TRUE;
                        break;
                    }
                }
                if (!found_alternative)
                {
                    op_extra_data->metadata_ip_buffer = NULL;
                }
            }
#endif /* INSTALL_METADATA */
            op_extra_data->input_buffer[term_idx] = NULL;
            vol_ctrl_recalc_main_buffer_size(op_extra_data, terminal_id);
        }
    }
    else
    {
        unsigned term_idx = terminal_id&TERMINAL_NUM_MASK;
#ifdef INSTALL_METADATA
        if (op_extra_data->metadata_op_buffer == op_extra_data->output_buffer[term_idx])
        {
            unsigned i;
            bool found_alternative = FALSE;
            for (i = 0; i < VOL_CTRL_CONSTANT_NUM_CHANNELS; i++)
            {
                if (i == term_idx)
                {
                    continue;
                }
                if (op_extra_data->output_buffer[i] != NULL && buff_has_metadata(op_extra_data->output_buffer[i]))
                {
                    op_extra_data->metadata_op_buffer = op_extra_data->output_buffer[i];
                    found_alternative = TRUE;
                    break;
                }
            }
            if (!found_alternative)
            {
                op_extra_data->metadata_op_buffer = NULL;
            }
        }
#endif /* INSTALL_METADATA */
        op_extra_data->output_buffer[term_idx] = NULL;
        vol_ctrl_recalc_main_buffer_size(op_extra_data, terminal_id);
    }

    return TRUE;
}

bool vol_ctlr_start(OPERATOR_DATA *op_data, void *message_data, unsigned *response_id, void **response_data)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);
    /* Setup Response to Start Request.   Assume Failure*/
    if (!base_op_build_std_response_ex(op_data, STATUS_OK, response_data))
    {
        return FALSE;
    }

    if (opmgr_op_is_running(op_data))
    {
        return TRUE;
    }

    if (!setup_processing(op_extra_data))
    {
        base_op_change_response_status(response_data, STATUS_CMD_FAILED);
    }

#ifdef VOLUME_CONTROL_AUX_TTP_SUPPORT
    /* Work out an estimate of downstream latency. This value is only
     * used when timestamp is invented by this operator to play voice
     * prompts at requested playback time.
     *
     * User can set that via DOWNSTREAM_LATENCY_EST parameter, if hasn't
     * been set by the user we will find a suitable value.
     */
    op_extra_data->downstream_latency_estimate =
        VOL_CTRL_DOWNSTREAM_LATENCY_EST(op_extra_data)*MILLISECOND;
    if(0 == op_extra_data->downstream_latency_estimate &&
       NULL != op_extra_data->metadata_op_buffer)
    {
        /* user hasn't set the parameter, a good achievable latency with the
         * assumption that the operator's output buffer is directly consumed
         * by a timed playback module at the end of chain is 75% of the buffers
         * size plus one system kick period.
         */
        unsigned buf_size = cbuffer_get_size_in_words(op_extra_data->metadata_op_buffer);
        op_extra_data->downstream_latency_estimate =
            convert_samples_to_time(buf_size, op_extra_data->sample_rate)*3/4 +
            stream_if_get_system_kick_period();
    }
    /* Reset timestamp references used for aux TTP*/
    op_extra_data->current_timestamp_valid = FALSE;
    op_extra_data->main_timestamp_valid = FALSE;
    op_extra_data->prev_consumed_samples = 0;
    op_extra_data->aux0_ttp.generate_ttp = FALSE;
#endif /* VOLUME_CONTROL_AUX_TTP_SUPPORT */

    return TRUE;
}

/* ************************************* Data processing-related functions and wrappers **********************************/

void destroy_processing(VOL_CTRL_DATA_T *op_extra_data)
{
    timer_cancel_event_atomic(&op_extra_data->pending_timer);

    if(!op_extra_data->channels)
    {
        return;
    }

    pfree(op_extra_data->channels);
    op_extra_data->channels = NULL;
}


static void vol_ctrl_setup_mute(VOL_CTRL_DATA_T *op_extra_data,unsigned bMute)
{
    int mute_increment;

    /*  cur_mute_gain = 0 at start, and default mute=FALSE
        To immediately unmute set mute_period=0
    */

    if((op_extra_data->sample_rate<1) || (op_extra_data->mute_period<1) )
    {
        /* Immediately set mute gain */
        op_extra_data->cur_mute_gain  = bMute ? 0 : FRACTIONAL(1.0);
        mute_increment=1;
    }
    else
    {

        if(op_extra_data->mute_period<1000)
        {
            mute_increment = pl_fractional_divide(op_extra_data->mute_period,1000);
            mute_increment = pl_fractional_divide(1,frac_mult(mute_increment,op_extra_data->sample_rate));
        }
        else
        {
            mute_increment = pl_fractional_divide(1,op_extra_data->sample_rate);
        }
    }

    /* Set direction of transition */
    op_extra_data->mute_increment = (bMute) ? -mute_increment : mute_increment;

}

bool setup_processing(VOL_CTRL_DATA_T   *op_extra_data)
{
    unsigned touched_sink = TOUCHED_NOTHING;
    unsigned touched_src  = TOUCHED_NOTHING;
    unsigned i,sink_bit,initial_channel_gain,chan_count=0;
    vol_ctrl_channel_t *chan_ptr;
	vol_ctrl_gains_t *lpvols =&op_extra_data->host_vol;

    patch_fn_shared(volume_control_wrapper);

    /* Release data object */
    destroy_processing(op_extra_data);


    /* Validate channel sinks and sources.  Count connected channels   */
    for(i=0,sink_bit=0;i<VOL_CTRL_CONSTANT_NUM_CHANNELS;i++,sink_bit+=2)
    {
        if(op_extra_data->input_buffer[i])
        {
            if(op_extra_data->output_buffer[i])
            {
                chan_count++;
                touched_sink |= ( TOUCHED_SINK_0 <<sink_bit );
                touched_src  |=  (TOUCHED_SOURCE_0 << i);
            }
            else
            {
                return FALSE;
            }
        }
        else if(op_extra_data->output_buffer[i])
        {
            return FALSE;
        }
    }

    /* Must have at least one channel */
    if(chan_count<1)
    {
        return FALSE;
    }

    /* A valid set of channels is connected */
    op_extra_data->channels = (vol_ctrl_channel_t*)xzpmalloc(chan_count*sizeof(vol_ctrl_channel_t));
    if(!op_extra_data->channels)
    {
        return FALSE;
    }
    op_extra_data->touched_src  = touched_src;
    op_extra_data->touched_sink = touched_sink;
    op_extra_data->num_channels = chan_count;

    /* Initialize Mute */
    op_extra_data->cur_mute_gain = 0;
    vol_ctrl_setup_mute(op_extra_data,lpvols->mute);

    /* Link Channels with channel object */
    chan_ptr = op_extra_data->channels;
    for(i=0;i<VOL_CTRL_CONSTANT_NUM_CHANNELS;i++)
    {
        if(op_extra_data->output_buffer[i])
        {
		    /* If initial value not specified before operator start, use -96db as default
			 * If an initial value is specified calculate channel gain as master_gain + channel_trim - post gain
			 */
		    if(op_extra_data->vol_initialised)
		    {
			    initial_channel_gain = dB60toLinearQ5(op_extra_data->lpvols->channel_trims[i] + op_extra_data->lpvols->master_gain - op_extra_data->post_gain);
			}
			else
			{
				initial_channel_gain = 0;
			};

			chan_ptr->chan_idx = i;
			chan_ptr->channel_gain = initial_channel_gain;
			chan_ptr->prim_mix_gain = dB60toLinearQ5(0); /*set to 0db. this will be recalculated as part of the update function*/
			chan_ptr->last_volume  = chan_ptr->channel_gain;
			chan_ptr++;
        }
    }

    op_extra_data->vol_initialised = 0;
    op_extra_data->wait_on_space_buffer=NULL;
    op_extra_data->wait_on_data_buffer=NULL;
    op_extra_data->aux_pending = FALSE;
    op_extra_data->used_all_input = FALSE;

    return TRUE;
}

void vol_ctlr_timer_task(void *kick_object)
{
    OPERATOR_DATA *op_data = (OPERATOR_DATA*) kick_object;
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);

    patch_fn_shared(volume_control_wrapper);

    base_op_profiler_start(op_data);

    op_extra_data->pending_timer = TIMER_ID_INVALID;

    /* Raise a bg int to process */
    opmgr_kick_operator(op_data);

    base_op_profiler_stop(op_data);
}

/**
 * \brief Hook for additional kicking in waiting situations
 */
static void vol_ctrl_kick_waiting(VOL_CTRL_DATA_T *op_extra_data, TOUCHED_TERMINALS *touched, VOL_CTRL_KICK_REASON reason)
{
    patch_fn_shared(volume_control_wrapper);
}

#ifdef INSTALL_METADATA
/**
 * \brief Handle aux metadata: if there is an eof tag in aux, return ref,
 *          delete all other aux metadata
 * \param op_extra_data     volume control operator specific data
 * \returns                 EOF tag, if found
 */
static metadata_tag* vol_ctrl_handle_aux_metadata(VOL_CTRL_DATA_T *op_extra_data)
{
    metadata_tag* aux_tag_head = NULL;
    metadata_tag* eoftag = NULL;
    metadata_tag* tmp_tag = NULL;
    metadata_tag* prev_tag = NULL;
    unsigned b4idx, afteridx;

    /* get aux metadata, if applicable */
    if (op_extra_data->metadata_aux_channel != NULL)
    {
        aux_tag_head = buff_metadata_remove(op_extra_data->metadata_aux_channel->buffer,
                                            op_extra_data->metadata_aux_channel->advance_buffer * OCTETS_PER_SAMPLE,
                                            &b4idx, &afteridx);
    }

    /* only look for EOF if we could output it */
    if (op_extra_data->metadata_op_buffer != NULL)
    {
        /* search for eof tag in aux metadata */
        tmp_tag = aux_tag_head;
        while (tmp_tag != NULL && !METADATA_STREAM_END(tmp_tag))
        {
            prev_tag = tmp_tag;
            tmp_tag = tmp_tag->next;
        }
        if (tmp_tag != NULL)
        {
            /* EOF tag was found in aux metadata*/
            eoftag = tmp_tag;
            /* remove it from the list */
            if (prev_tag != NULL)
            {
                prev_tag->next = eoftag->next;
            }
            else
            {
                aux_tag_head = eoftag->next;
            }
            eoftag->next = NULL;
        }
    }
    /* delete aux metadata anyways */
    buff_metadata_tag_list_delete(aux_tag_head);

    return eoftag;
}
#ifdef VOLUME_CONTROL_AUX_TTP_SUPPORT
/**
 * \brief timestamps a VOID tag and turn it to a real TIMESTAMP tag.
 * \param op_extra_data volume control operator specific data
 * \param mtag taglist that should be timestamped
 * \octets_before_tag octets before mtag first octet
 */
static void vol_ctrl_timestamp_void_tags(VOL_CTRL_DATA_T *op_extra_data,
                                         metadata_tag *mtag,
                                         unsigned octets_before_tag)
{
    while(NULL != mtag)
    {
        /* traverse through all tags in the list*/
        if(IS_VOID_TTP_TAG(mtag))
        {
            /* tags is a VOID tag, turn it to a TIMESTAMP tags.
             * Note: current_timestamp field holds ttp for octet 0
             */
            TIME_INTERVAL time_passed =
                convert_samples_to_time(octets_before_tag/ADDR_PER_WORD,
                                        op_extra_data->sample_rate);
            METADATA_TIME_OF_ARRIVAL_UNSET(mtag);
            METADATA_TIMESTAMP_SET(mtag,
                                   time_add(op_extra_data->current_timestamp, time_passed),
                                   METADATA_TIMESTAMP_LOCAL);
        }
        octets_before_tag += mtag->length;
        mtag = mtag->next;
    }
}

/**
 * \brief updates the timestamp that is going to be used for timed
 *        playback of aux inputs.
 * \param op_extra_data volume control operator specific data
 * \param amount_consumed amount of input consumed since last update
 */
static void vol_ctrl_update_current_timestamp(VOL_CTRL_DATA_T *op_extra_data)
{
    unsigned amount_consumed = op_extra_data->prev_consumed_samples;
    /* This will be set again when op consumes samples from input */
    op_extra_data->prev_consumed_samples = 0;

/*
 * NOTE: While timestamp is only required for when operator is handling
 *       an auxiliary with ttp enabled, we need to keep updating it all
 *       the time so we will have the most accurate valid timestamp
 *       when we need it for mixing. This will add tiny bit of overhead
 *       to the operator.
 */
#if 0
    if(!op_extra_data->aux0_ttp.enabled)
    {
        /* We don't need timestamp if TTP isn't enabled */
        op_extra_data->current_timestamp_valid = FALSE;
        return;
    }
#endif

    /* 1. See if we see timestamp in input buffer, if we do then that will
     *    be used for timed mixing of aux input.
     */
    unsigned b4idx = 0;
    metadata_tag *mtag = buff_metadata_peek_ex(op_extra_data->metadata_ip_buffer, &b4idx);
    while(mtag != NULL)
    {
        /* Search the input buffer metadata, if we see a TTP tag, then we use input time
         * stamps for the purpose of mixing time of auxiliary inputs to main inputs.
         */
        if(IS_TIMESTAMPED_TAG(mtag))
        {
            unsigned *err_offset_id;
            unsigned out_length;
            /* go back to first input sample */
            TIME_INTERVAL time_back = convert_samples_to_time(b4idx / OCTETS_PER_SAMPLE,
                                                              op_extra_data->sample_rate);
            op_extra_data->current_timestamp = time_sub(mtag->timestamp, time_back);
            if (buff_metadata_find_private_data(mtag, META_PRIV_KEY_TTP_OFFSET, &out_length,
                                                (void **)&err_offset_id))
            {
                int *err_offset_ptr = ttp_info_get(*err_offset_id);
                if (err_offset_ptr != NULL)
                {
                    /* subtract the offset */
                    op_extra_data->current_timestamp =
                        time_sub(op_extra_data->current_timestamp, *err_offset_ptr);
                }
            }
            /* current_timestamp: time stamp associated with first sample of input
             * main_timestamp_valid: whether main input has valid timestamp
             * last_main_real_timestamp: last valid timestamp we received from main input
             */
            op_extra_data->last_main_real_timestamp =
                op_extra_data->current_timestamp;
            op_extra_data->main_timestamp_valid = TRUE;
            op_extra_data->current_timestamp_valid = TRUE;

            /* real timestamp from input update, no accumulated remainder*/
            op_extra_data->current_timestamp_update_remainder = 0;

            /* Op isn't generating ttp itself */
            op_extra_data->aux0_ttp.generate_ttp = FALSE;
            return;
        }
        b4idx += mtag->length;
        mtag = mtag->next;
    }

    /* 2. If we don't have timestamp from input buffer see if
     *    we can build based on last received timestamp from main
     *    buffer. We stop relying on main timestamp if we
     *    haven't received any timestamp from input in last
     *    MAX_REBUILD_TIMESTAMP_PERIOD milliseconds.
     */
    if(op_extra_data->main_timestamp_valid)
    {
        TIME_INTERVAL time_passed_since_last_real_timestamp =
            time_sub(op_extra_data->current_timestamp,
                     op_extra_data->last_main_real_timestamp);
        if(time_passed_since_last_real_timestamp >
           (int)(VOL_CTRL_AUX_USE_MAIN_TIMESTAMP_PERIOD_MS*MILLISECOND))
        {
            /* stop using main timestamp */
            op_extra_data->main_timestamp_valid = FALSE;
            op_extra_data->current_timestamp_valid = FALSE;
            op_extra_data->current_timestamp_update_remainder = 0;
        }
    }

    /* If aren't receiving timestamp from main input,
     * see if op should generate timestamp itself.
     * It does so if aux timed playback has been enabled
     * by the user and also the operator hasn't yet started
     * to process actual aux data.
     */
    if(!op_extra_data->main_timestamp_valid &&
       op_extra_data->aux0_ttp.enabled &&
       !op_extra_data->aux0_ttp.generate_ttp &&
       !op_extra_data->aux_active)
    {
        /* Start self ttp generating, with an estimated
         * target latency from current time.
         */
        TIME cur_time = time_get_time();
        op_extra_data->current_timestamp_valid = TRUE;
        op_extra_data->current_timestamp =
            time_add(cur_time, op_extra_data->downstream_latency_estimate);
        op_extra_data->aux0_ttp.generate_ttp = TRUE;
        return;
    }

    /* overall timestamp is valid if we receive
     * timestamp from input or op is generating
     * timestamp itself. Only one is valid at one time,
     * if input has valid timestamp op will not
     * generate ttp itself.
     * */
    op_extra_data->current_timestamp_valid =
        op_extra_data->main_timestamp_valid ||
        op_extra_data->aux0_ttp.generate_ttp;

    /* Build timestamp from previous one considering number of samples
     * consumed since
     */
    if(op_extra_data->current_timestamp_valid && amount_consumed != 0)
    {
        /* new_time_stamp = previous_time_stamp + amount_consumed/sample_rate
         * we keep a remainder to avoid the effect of truncation.
         */
        uint64 tm_mul = (uint64)amount_consumed * 1000000 +
            op_extra_data->current_timestamp_update_remainder;
        TIME_INTERVAL time_passed = (TIME_INTERVAL) (tm_mul/op_extra_data->sample_rate);
        op_extra_data->current_timestamp_update_remainder =
            (unsigned)(tm_mul - ((uint64)time_passed*op_extra_data->sample_rate));
        op_extra_data->current_timestamp =
            time_add(op_extra_data->current_timestamp, time_passed);
    }
}
#endif /* #ifdef VOLUME_CONTROL_AUX_TTP_SUPPORT */
/**
 * \brief Transport metadata from input to output and handle aux EOF.
 * \param op_extra_data     volume control operator specific data
 * \param eoftag            EOF tag, if found in aux metadata
 */
static void vol_ctrl_handle_input_metadata(VOL_CTRL_DATA_T *op_extra_data, metadata_tag* eoftag)
{
    metadata_tag *ret_mtag;
    metadata_tag* tmp_tag = NULL;
    metadata_tag* prev_tag = NULL;
    unsigned b4idx, afteridx;
    unsigned input_amount = op_extra_data->tc.num_words * OCTETS_PER_SAMPLE;

    /* get input metadata, if applicable*/
    if (op_extra_data->metadata_ip_buffer != NULL)
    {
        ret_mtag = buff_metadata_remove(op_extra_data->metadata_ip_buffer,
                                        input_amount, &b4idx, &afteridx);
    }
    else
    {
        b4idx = 0;
        afteridx = input_amount;
        ret_mtag = NULL;
    }

    if (op_extra_data->metadata_op_buffer != NULL)
    {
#ifdef VOLUME_CONTROL_AUX_TTP_SUPPORT
        if(op_extra_data->aux0_ttp.generate_ttp)
        {
            /* Operator shall generate timestamp itself, this
             * is done by time-stamping VOID tags.
             */
            vol_ctrl_timestamp_void_tags(op_extra_data, ret_mtag, b4idx);
        }
#endif
        if (ret_mtag == NULL)
        {
            /* No metadata in input: we cannot append EOF tags!
             * In this case we need to save the EOF tag until
             * a new input tag comes in.
             * If none does, we need to recognise the data closing
             * the last received tag and append the EOF there. */
            if (eoftag != NULL)
            {
                /* Save it for next run. */
                if (op_extra_data->last_eoftag != NULL)
                {
                    /* We already had a pending EOF tag. Delete it. */
                    buff_metadata_tag_list_delete(op_extra_data->last_eoftag);
                }
                op_extra_data->last_eoftag = eoftag;
                L2_DBG_MSG("volume_control: aux EOF saved for next run");
            }
            if (op_extra_data->last_tag_data_remaining <= input_amount)
            {
                /* The last received tag is closed now and no other tags
                 * came through. If we had a pending EOF tag,
                 * we need to output it now */
                if (op_extra_data->last_eoftag != NULL)
                {
                    ret_mtag = op_extra_data->last_eoftag;
                    /* clear backup ptr */
                    op_extra_data->last_eoftag = NULL;
                    b4idx = input_amount;
                    afteridx = 0;
                    L2_DBG_MSG("volume_control: ORPHAN aux EOF moved to output");
                }
                op_extra_data->last_tag_data_remaining = 0;
            }
            else
            {
                /* update the number of octets to come to complete the last tag */
                op_extra_data->last_tag_data_remaining -= input_amount;
            }
        }
        else
        {
            /* Check if we found an EOF on a previous run where input had no
             * metadata and we could not append it on the output. */
            if (op_extra_data->last_eoftag != NULL && ret_mtag != NULL)
            {
                /* Transport now at the beginning of the output*/
                op_extra_data->last_eoftag->next = ret_mtag;
                ret_mtag = op_extra_data->last_eoftag;
                /* clear backup ptr */
                op_extra_data->last_eoftag = NULL;
                L2_DBG_MSG("volume_control: previous aux EOF moved to output");
            }

            /* find the last tag of input metadata */
            tmp_tag = ret_mtag;
            prev_tag = NULL;
            while (tmp_tag->next != NULL)
            {
                prev_tag = tmp_tag;
                tmp_tag = tmp_tag->next;
            }

            /* save the number of octets to come to complete the last tag*/
            if (afteridx > 0 && afteridx <= tmp_tag->length)
            {
                op_extra_data->last_tag_data_remaining = tmp_tag->length - afteridx;
            }
            else
            {
                op_extra_data->last_tag_data_remaining = 0;
            }

            if (eoftag != NULL)
            {
                /* We found an EOF on the aux channel on this run
                 * bring it on the output in second to last position:
                 * aux_tag is the last tag, place eof tag in front of it. */
                eoftag->next = tmp_tag;
                if (prev_tag == NULL)
                {
                    ret_mtag = eoftag;
                }
                else
                {
                    prev_tag->next = eoftag;
                }
                L2_DBG_MSG("volume_control: aux EOF moved to output");
            }
        }
        /* write metadata to output */
        buff_metadata_append(op_extra_data->metadata_op_buffer, ret_mtag, b4idx, afteridx);
    }
    else
    {
        /* we cannot output metadata, delete input metadata */
        buff_metadata_tag_list_delete(ret_mtag);
    }
}
#endif /* INSTALL_METADATA */

#ifdef VOLUME_CONTROL_AUX_TIMING_TRACE
static uint32 pack_aux_state(VOL_CTRL_DATA_T *op_extra_data)
{
    VOL_CTRL_PACKED_AUX_STATE packed_aux_state;

    packed_aux_state.u.f.aux_state = op_extra_data->aux_channel[0].state;
    packed_aux_state.u.f.aux_pending = op_extra_data->aux_pending;
#ifdef VOLUME_CONTROL_AUX_TTP_SUPPORT
    packed_aux_state.u.f.ttp_enabled = op_extra_data->aux0_ttp.enabled;
    packed_aux_state.u.f.generate_ttp = op_extra_data->aux0_ttp.generate_ttp;
#endif

    return packed_aux_state.u.w;
}

static void record_aux_ttr_event(OPERATOR_DATA *op_data,
                                 TIMING_TRACE_OP_EVENT_TYPE event_type,
                                 unsigned arg1,
                                 unsigned arg2)
{
    /* Always use the ID for sink terminal 1 */
    ENDPOINT_ID aux_in_ep_id = base_op_get_ext_op_id(op_data) |
            STREAM_EP_EXT_SINK | VOL_CTRL_TTR_EVENT_TERMINAL_NUM;
    opmgr_record_timing_trace_op_term_event(aux_in_ep_id, event_type, arg1, arg2);
}
#else /* VOLUME_CONTROL_AUX_TIMING_TRACE */
static inline void record_aux_ttr_event(OPERATOR_DATA *op_data,
                                        TIMING_TRACE_OP_EVENT_TYPE event_type,
                                        unsigned arg1,
                                        unsigned arg2)
{
}
#endif /* VOLUME_CONTROL_AUX_TIMING_TRACE */

RUN_FROM_PM_RAM
void vol_ctlr_process_data(OPERATOR_DATA *op_data, TOUCHED_TERMINALS *touched)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);
    unsigned         i,samples_to_process,num_channels,amount,touched_sink;
    unsigned         block_size = VOL_CTRL_BLOCK_SIZE;

    patch_fn(volume_control_process_data_patch);

    op_extra_data->used_all_input = FALSE;

#ifdef VOLUME_CONTROL_AUX_TIMING_TRACE
    uint32 packed_aux_state = pack_aux_state(op_extra_data);
#endif

#ifdef VOLUME_CONTROL_AUX_TTP_SUPPORT
    /* update timestamp for using aux TTP */
    vol_ctrl_update_current_timestamp(op_extra_data);
#endif

    /* Accelerators for data/space */
    samples_to_process = MAXINT;
    if(op_extra_data->stream_based)
    {
        num_channels = 1;
    }
    else
    {
        if(op_extra_data->wait_on_space_buffer)
        {
            samples_to_process = cbuffer_calc_amount_space_in_words(op_extra_data->wait_on_space_buffer);
            if(samples_to_process<block_size)
            {
                vol_ctrl_kick_waiting(op_extra_data, touched, VOL_CTRL_KICK_WAIT_ON_SPACE_LATE);
                return;
            }
            op_extra_data->wait_on_space_buffer=NULL;
        }
        if(op_extra_data->wait_on_data_buffer)
        {
            amount = cbuffer_calc_amount_data_in_words(op_extra_data->wait_on_data_buffer);
            if(amount<block_size)
            {
                vol_ctrl_kick_waiting(op_extra_data, touched, VOL_CTRL_KICK_WAIT_ON_DATA_LATE);
                return;
            }
            op_extra_data->wait_on_data_buffer=NULL;
        }
        num_channels = op_extra_data->num_channels;
    }

    /* Compute channel transfer amount */
    for(i=0;i<num_channels;i++)
    {
        unsigned term_idx = op_extra_data->channels[i].chan_idx;
        tCbuffer *buffer;

      buffer = op_extra_data->input_buffer[term_idx];
      amount = cbuffer_calc_amount_data_in_words(buffer);
      if(amount<=samples_to_process)
      {
            if(amount < block_size)
            {
                op_extra_data->wait_on_data_buffer = buffer;
                vol_ctrl_kick_waiting(op_extra_data, touched, VOL_CTRL_KICK_WAIT_ON_DATA);
                return;
            }
            samples_to_process=amount;
            op_extra_data->used_all_input = TRUE;
        }

        buffer = op_extra_data->output_buffer[term_idx];
        amount = cbuffer_calc_amount_space_in_words(buffer);

        /* Relatively likely to need changes below */
        patch_fn(volume_control_adjust_amount);

        if(amount<samples_to_process)
        {
            if(amount<block_size)
            {
                op_extra_data->wait_on_space_buffer = buffer;
                vol_ctrl_kick_waiting(op_extra_data, touched, VOL_CTRL_KICK_WAIT_ON_SPACE);
                return;
            }
            samples_to_process=amount;
            op_extra_data->used_all_input = FALSE;
        }
    }

    /* Update AUX state.
       If the aux stream doesn't have enough data it indicates a state change */
    unsigned aux_check = (op_extra_data->aux_active | op_extra_data->aux_connected)
        & VOL_CTRL_CHANNEL_MASK;

    /* Relatively likely to need changes below */
    patch_fn(volume_control_aux_check);

    op_extra_data->tc.num_words = samples_to_process;
    
    if (aux_check != 0)
    {
        vol_ctrl_aux_channel_t* aux_ch;
        unsigned aux_kick = 0;
        unsigned aux_limit = MAXINT;

        for (i = 0, aux_ch = &op_extra_data->aux_channel[0];
             i < VOL_CTRL_CONSTANT_NUM_CHANNELS;
             i += 1, aux_ch += 1)
        {
            if (aux_ch->buffer == NULL)
            {
                aux_ch->advance_buffer = 0;
            }
            else
            {
                amount = cbuffer_calc_amount_data_in_words(aux_ch->buffer);
                if (op_extra_data->metadata_aux_channel == aux_ch)
                {
                    amount = pl_min(
                            amount,
                            buff_metadata_available_octets(
                                    aux_ch->buffer)
                                    / OCTETS_PER_SAMPLE);
                }
                aux_ch->advance_buffer = amount;

                /* Kick back any aux inputs which have no data, regardless
                 * of state. This helps to prime aux sources which have not
                 * started yet, or recover from false stall detections.
                 */
                if (amount == 0)
                {
                    touched->sinks |= (2 << (2 * i));
                }

                /* Only active aux inputs limit the amount of data to process,
                 * because in other states, aux data is not consumed.
                 */
                if (aux_ch->state == AUX_STATE_IN_AUX)
                {
                    aux_limit = pl_min(aux_limit, amount);
                    if (amount < samples_to_process)
                    {
                        aux_kick |= (2 << (2 * i));
                    }
                }
            }
        }

        /* Allow an aux input to stall if it does not produce data within
         * 1/2 kick period of being kicked backwards (details see B-255916).
         * Currently there is one overall state for aux_pending, which means
         * that the handling of more than one independent aux source is
         * likely to be flawed (B-255917).
         */
        if (aux_limit >= samples_to_process)
        {
            op_extra_data->aux_pending = FALSE;
        }
        else
        {
            if (op_extra_data->aux_pending && (aux_limit == 0))
            {
                /* Since entering aux_pending state, no aux data has arrived */
                if (op_extra_data->pending_timer != TIMER_ID_INVALID)
                {
                    vol_ctrl_kick_waiting(op_extra_data, touched, VOL_CTRL_KICK_STILL_AUX_PENDING);
                    return;
                }
                else
                {
                    /* Waited too long; let vol_ctrl_update_aux_state handle
                     * an aux stop */
                    op_extra_data->aux_pending = FALSE;
                }
            }
            else
            {
                if (op_extra_data->aux_pending)
                {
                    /* Cleanup the timer before restarting it */
                    timer_cancel_event_atomic(&op_extra_data->pending_timer);
                }

                /* (Re-)Start aux_pending */
                touched->sinks |= aux_kick;
                op_extra_data->pending_timer = timer_schedule_event_in(
                    stream_if_get_system_kick_period() / 2,
                    vol_ctlr_timer_task, (void*)op_data );
                op_extra_data->aux_pending = TRUE;
                vol_ctrl_kick_waiting(op_extra_data, touched, VOL_CTRL_KICK_AUX_PENDING);

                /* Proceed with processing; consume all data from at least one
                 * of the active aux inputs.
                 */
                op_extra_data->tc.num_words = aux_limit;
            }
        }

        /* Time constants based on samples to process and sample rate */
        vol_ctrl_compute_time_constants(op_extra_data->sample_rate,
                                        op_extra_data->parameters.OFFSET_VOLUME_TC,
                                        &op_extra_data->tc);

        /* Now all aux inputs have enough data, or some aux inputs were left
         * pending once and some still don't have enough data, so those now
         * have to do a stopping transition. Amount of available data is in
         * each aux channel's advance_buffer field.
         */
#ifdef VOLUME_CONTROL_AUX_TTP_SUPPORT
        /* There is a possibility that vol_ctrl_update_aux_state
         * limits the number of samples to process, so keep a copy
         * of that now.
         */
        samples_to_process = op_extra_data->tc.num_words;
#endif

        vol_ctrl_update_aux_state(op_extra_data, aux_check, &op_extra_data->tc);

#ifdef VOLUME_CONTROL_AUX_TTP_SUPPORT
        /* whole of this block is for debugging purpose only,
         * showing to user whether aux is being mixed in timed mode
         * or normal mode.
         */
        if(op_extra_data->dbg_aux_mixing_started)
        {
            if(op_extra_data->current_timestamp_valid && op_extra_data->aux0_ttp.enabled)
            {
                L2_DBG_MSG3("Volume Control: Started mixing auxiliary input in playback mode, "
                            "requested ttp: %d, mixed at=%d, diff=%d",
                            op_extra_data->aux0_ttp.time_to_play,
                            op_extra_data->current_timestamp,
                            time_sub(op_extra_data->current_timestamp,
                                     op_extra_data->aux0_ttp.time_to_play));

                record_aux_ttr_event(op_data,
                                     VOL_CTRL_TTR_EVENT_AUX_STARTED_MIXING,
                                     op_extra_data->aux0_ttp.time_to_play,
                                     op_extra_data->current_timestamp);
            }
            else
            {
                L2_DBG_MSG("Volume Control: Started mixing auxiliary input in non-timed playback mode");

                record_aux_ttr_event(op_data,
                                     VOL_CTRL_TTR_EVENT_AUX_STARTED_MIXING,
                                     0,
                                     0);
            }
            op_extra_data->dbg_aux_mixing_started = FALSE;
        }

        if(samples_to_process > op_extra_data->tc.num_words)
        {
            /* Aux timed playback limited amount to consume, this happens only once
             * right at the start of mixing and is because we want to start mixing
             * at a sample with timestamp close to the requested time to play.
             */

            /* recalculate time constants based on limited amount */
            vol_ctrl_compute_time_constants(op_extra_data->sample_rate,
                                            op_extra_data->parameters.OFFSET_VOLUME_TC,
                                            &op_extra_data->tc);

            /* Also do a self kick to process the rest of chunk immediately.*/
            if (op_extra_data->pending_timer == TIMER_ID_INVALID)
            {
                op_extra_data->pending_timer =
                    timer_schedule_event_in(0, vol_ctlr_timer_task, (void*)op_data );
            }
        }
#endif  /* VOLUME_CONTROL_AUX_TTP_SUPPORT  */

    }
    else
    {
        op_extra_data->aux_pending = FALSE;

        /* Time constants based on samples to process and sample rate */
        vol_ctrl_compute_time_constants(op_extra_data->sample_rate,
                                        op_extra_data->parameters.OFFSET_VOLUME_TC,
                                        &op_extra_data->tc);
    }

    if (! op_extra_data->aux_pending)
    {
        timer_cancel_event_atomic(&op_extra_data->pending_timer);
    }

#ifdef VOLUME_CONTROL_AUX_TTP_SUPPORT
    /* update amount that is consumed this run */
    op_extra_data->prev_consumed_samples = op_extra_data->tc.num_words;
#endif
    if (op_extra_data->tc.num_words > 0)
    {
#ifdef INSTALL_METADATA
        metadata_tag* eoftag = vol_ctrl_handle_aux_metadata(op_extra_data);
        vol_ctrl_handle_input_metadata(op_extra_data, eoftag);
#endif  /* INSTALL_METADATA */

        /* Update Main Channels */
        vol_ctrl_update_channel(op_extra_data,op_extra_data->channels,op_extra_data->lpvols,&op_extra_data->tc);
    }

#ifdef VOLUME_CONTROL_AUX_TIMING_TRACE
    uint32 updated_aux_state = pack_aux_state(op_extra_data);
    if (updated_aux_state != packed_aux_state)
    {
        record_aux_ttr_event(op_data, VOL_CTRL_TTR_EVENT_AUX_STATE,
                             updated_aux_state, 0);
    }
#endif /* VOLUME_CONTROL_AUX_TIMING_TRACE */

    /* Handle backwards kicks for main channels */
    touched_sink = (op_extra_data->used_all_input) ? op_extra_data->touched_sink : 0;

    /* Update Aux Buffers */
    op_extra_data->aux_state   = 0;
    amount    = op_extra_data->aux_connected;
    if(amount)
    {
        vol_ctrl_aux_channel_t *aux_ptr=op_extra_data->aux_channel;
        i = 0;
        do
        {
            if(aux_ptr->state==AUX_STATE_IN_AUX)
            {
                unsigned index_mask = (1<<i);
                op_extra_data->aux_state |= index_mask;
                // only advance if used by a channel?
                if(!(op_extra_data->aux_in_use&index_mask))
                {
                    aux_ptr->advance_buffer=0;
                }
            }
            if(aux_ptr->advance_buffer)
            {
                touched_sink |= ( TOUCHED_SINK_0 << ((i<<1)+1) );
                cbuffer_advance_read_ptr(aux_ptr->buffer,aux_ptr->advance_buffer);
            }
            i++;
            aux_ptr++;
            amount>>=1;
        }while(amount);
    }

   touched->sinks |= touched_sink;
   touched->sources = op_extra_data->touched_src;

}


/* **************************** Operator message handlers ******************************** */

bool vol_ctlr_opmsg_obpm_set_control(OPERATOR_DATA *op_data, void *message_data, unsigned *resp_length, OP_OPMSG_RSP_PAYLOAD **resp_data)
{
    VOL_CTRL_DATA_T    *op_extra_data = get_instance_data(op_data);
    unsigned            i,num_controls,cntrl_value;
    CPS_CONTROL_SOURCE  cntrl_src;
    OPMSG_RESULT_STATES result = OPMSG_RESULT_STATES_NORMAL_STATE;
    vol_ctrl_gains_t *lpvols =&op_extra_data->host_vol;
    bool              bIsOBPM=FALSE;

    patch_fn(volume_control_opmsg_obpm_set_control_patch);

    if(!cps_control_setup(message_data, resp_length, resp_data,&num_controls))
    {
        return FALSE;
    }

    for(i=0;i<num_controls;i++)
    {
        unsigned  cntrl_id=cps_control_get(message_data,i,&cntrl_value,&cntrl_src);

        /* Check for OBPM and Override.   Override is all or none, not per control */
        if((i==0)&&(cntrl_src != CPS_SOURCE_HOST))
        {
            bIsOBPM = TRUE;
            lpvols  = &op_extra_data->obpm_vol;
            /* Polarity of override enable/disable is inverted */
            op_extra_data->Ovr_Control = (cntrl_src == CPS_SOURCE_OBPM_DISABLE) ?  VOL_CTRL_CONTROL_VOL_OVERRIDE : 0;
        }

        if(cntrl_id==VOL_CTRL_CONSTANT_POST_GAIN_CTRL)
        {
            if(bIsOBPM)
            {
                /* OBPM can not set post gain */
                result = OPMSG_RESULT_STATES_UNSUPPORTED_CONTROL;
                break;
            }

            op_extra_data->post_gain = cntrl_value;

            op_extra_data->shared_volume_ptr->inv_post_gain = dB60toLinearQ5(-op_extra_data->post_gain);
        }
        else if(cntrl_id==VOL_CTRL_CONSTANT_MASTER_GAIN_CTRL)
        {
            lpvols->master_gain = cntrl_value;
            if (!opmgr_op_is_running(op_data))
            {
                op_extra_data->vol_initialised = 1;
            }
        }
        else if(cntrl_id==OPMSG_CONTROL_MUTE_ID)
        {
            lpvols->mute = cntrl_value;
            if (opmgr_op_is_running(op_data))
            {
                vol_ctrl_setup_mute(op_extra_data,lpvols->mute);
            }
        }
        else if(cntrl_id==VOL_CTRL_CONSTANT_MUTE_PERIOD_CTRL)
        {
            op_extra_data->mute_period = cntrl_value;
            if (opmgr_op_is_running(op_data))
            {
                vol_ctrl_setup_mute(op_extra_data,lpvols->mute);
            }
        }
        else if( (cntrl_id>=VOL_CTRL_CONSTANT_TRIM1_GAIN_CTRL) &&
                 (cntrl_id<=VOL_CTRL_CONSTANT_TRIM8_GAIN_CTRL) )
        {
            lpvols->channel_trims[cntrl_id-VOL_CTRL_CONSTANT_TRIM1_GAIN_CTRL] = cntrl_value;
        }
        else if( (cntrl_id>=VOL_CTRL_CONSTANT_AUX_GAIN_CTRL1) &&
                 (cntrl_id<=VOL_CTRL_CONSTANT_AUX_GAIN_CTRL8) )
        {
            lpvols->auxiliary_gain[cntrl_id-VOL_CTRL_CONSTANT_AUX_GAIN_CTRL1] = cntrl_value;
        }
        else
        {
            result = OPMSG_RESULT_STATES_UNSUPPORTED_CONTROL;
            break;
        }
    }

    if(op_extra_data->Ovr_Control&VOL_CTRL_CONTROL_VOL_OVERRIDE)
    {
        op_extra_data->lpvols = &op_extra_data->obpm_vol;
    }
    else
    {
        op_extra_data->lpvols = &op_extra_data->host_vol;
    }
    op_extra_data->shared_volume_ptr->current_volume_level = op_extra_data->lpvols->master_gain;


    cps_response_set_result(resp_data,result);

    return TRUE;
}

bool vol_ctlr_opmsg_obpm_get_params(OPERATOR_DATA *op_data, void *message_data, unsigned *resp_length, OP_OPMSG_RSP_PAYLOAD **resp_data)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);

    return cpsGetParameterMsgHandler(&op_extra_data->parms_def ,message_data, resp_length,resp_data);
}

bool vol_ctlr_opmsg_obpm_get_defaults(OPERATOR_DATA *op_data, void *message_data, unsigned *resp_length, OP_OPMSG_RSP_PAYLOAD **resp_data)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);

    return cpsGetDefaultsMsgHandler(&op_extra_data->parms_def ,message_data, resp_length,resp_data);
}

bool vol_ctlr_opmsg_obpm_set_params(OPERATOR_DATA *op_data, void *message_data, unsigned *resp_length, OP_OPMSG_RSP_PAYLOAD **resp_data)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);
    bool retval;

    patch_fn(volume_control_opmsg_obpm_set_params_patch);

    retval = cpsSetParameterMsgHandler(&op_extra_data->parms_def ,message_data, resp_length,resp_data);

    /* Set the Reinit flag after setting the parameters */
    op_extra_data->ReInitFlag = 1;

    return retval;
}

bool vol_ctlr_opmsg_obpm_get_status(OPERATOR_DATA *op_data, void *message_data, unsigned *resp_length, OP_OPMSG_RSP_PAYLOAD **resp_data)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);
    vol_ctrl_gains_t *lpvols = op_extra_data->lpvols;
    unsigned  *resp;

    patch_fn_shared(volume_control_wrapper);

    if(!common_obpm_status_helper(message_data,resp_length,resp_data,sizeof(VOL_CTRL_STATISTICS),&resp))
    {
        return FALSE;
    }

    if (resp)
    {
        resp = cpsPack2Words(op_extra_data->Ovr_Control, op_extra_data->post_gain, resp);
        resp = cpsPack2Words(lpvols->master_gain, lpvols->auxiliary_gain[0], resp);
        resp = cpsPack2Words(lpvols->auxiliary_gain[1], lpvols->auxiliary_gain[2], resp);
        resp = cpsPack2Words(lpvols->auxiliary_gain[3], lpvols->auxiliary_gain[4], resp);
        resp = cpsPack2Words(lpvols->auxiliary_gain[5], lpvols->auxiliary_gain[6], resp);
        resp = cpsPack2Words(lpvols->auxiliary_gain[7], lpvols->channel_trims[0], resp);
        resp = cpsPack2Words(lpvols->channel_trims[1], lpvols->channel_trims[2], resp);
        resp = cpsPack2Words(lpvols->channel_trims[3], lpvols->channel_trims[4], resp);
        resp = cpsPack2Words(lpvols->channel_trims[5], lpvols->channel_trims[6], resp);
        resp = cpsPack2Words(lpvols->channel_trims[7], op_extra_data->aux_state, resp);
    }

    return TRUE;
}



static bool ups_params_vc(void* instance_data,PS_KEY_TYPE key,PERSISTENCE_RANK rank,
                          uint16 length, unsigned* data, STATUS_KYMERA status,uint16 extra_status_info)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data((OPERATOR_DATA*)instance_data);

    cpsSetParameterFromPsStore(&op_extra_data->parms_def,length,data,status);

    /* Set the Reinit flag after setting the parameters */
    op_extra_data->ReInitFlag = 1;

    return TRUE;
}

bool vol_ctlr_opmsg_set_ucid(OPERATOR_DATA *op_data, void *message_data, unsigned *resp_length, OP_OPMSG_RSP_PAYLOAD **resp_data)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);
    PS_KEY_TYPE key;
    bool retval;

    retval = cpsSetUcidMsgHandler(&op_extra_data->parms_def,message_data,resp_length,resp_data);

    key = MAP_CAPID_UCID_SBID_TO_PSKEYID(base_op_get_cap_id(op_data),op_extra_data->parms_def.ucid,OPMSG_P_STORE_PARAMETER_SUB_ID);
    ps_entry_read((void*)op_data,key,PERSIST_ANY,ups_params_vc);

    return retval;
}

bool vol_ctlr_opmsg_get_ps_id(OPERATOR_DATA *op_data, void *message_data, unsigned *resp_length, OP_OPMSG_RSP_PAYLOAD **resp_data)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);

    return cpsGetUcidMsgHandler(&op_extra_data->parms_def,base_op_get_cap_id(op_data),message_data,resp_length,resp_data);
}

bool vol_ctlr_opmsg_set_sample_rate(OPERATOR_DATA *op_data, void *message_data, unsigned *resp_length, OP_OPMSG_RSP_PAYLOAD **resp_data)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);

    /* We received client ID, length and then opmsgID and OBPM params */
    op_extra_data->sample_rate = 25 * (OPMSG_FIELD_GET(message_data, OPMSG_COMMON_MSG_SET_SAMPLE_RATE, SAMPLE_RATE));

    return TRUE;
}

bool vol_ctlr_opmsg_data_stream_based(OPERATOR_DATA *op_data, void *message_data, unsigned *resp_length, OP_OPMSG_RSP_PAYLOAD **resp_data)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);

    /* We received client ID, length and then opmsgID and OBPM params */
    op_extra_data->stream_based = OPMSG_FIELD_GET(message_data, OPMSG_COMMON_MSG_SET_DATA_STREAM_BASED, IS_STREAM_BASED);

    return TRUE;
}
#ifdef VOLUME_CONTROL_AUX_TTP_SUPPORT
bool vol_ctlr_opmsg_set_aux_time_to_play(OPERATOR_DATA *op_data, void *message_data, unsigned *resp_length, OP_OPMSG_RSP_PAYLOAD **resp_data)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);


    /* get aux channel index */
    unsigned aux_channel_index = OPMSG_FIELD_GET(message_data,
                                                 OPMSG_VOL_CTRL_SET_AUX_TIME_TO_PLAY,
                                                 AUX_CHANNEL_INDEX);

    /* requested play back time for aux channel */
    TIME aux_time_to_play =
        (TIME)OPMSG_FIELD_GET_FROM_OFFSET(message_data,
                                          OPMSG_VOL_CTRL_SET_AUX_TIME_TO_PLAY,
                                          AUX_TIME_TO_PLAY, 1) +
        ((TIME)OPMSG_FIELD_GET_FROM_OFFSET(message_data,
                                           OPMSG_VOL_CTRL_SET_AUX_TIME_TO_PLAY,
                                          AUX_TIME_TO_PLAY, 0) << 16);
    /* get drift rate */
    int aux_drift_rate = (int)(int16)OPMSG_FIELD_GET(message_data,
                                                     OPMSG_VOL_CTRL_SET_AUX_TIME_TO_PLAY,
                                                     AUX_DRIFT_RATE);

    /* Only channel 0 is supported */
    if(aux_channel_index != 0)
    {
        L2_DBG_MSG1("Volume Control Aux Timed Playback, "
                    "only channel 0 supports this feature, channel=%d",
                    aux_channel_index);
        return FALSE;
    }

    /* drift_rate is in 1/10 of ppm, so convert it to a
     * fractional number.
     */
    aux_drift_rate = aux_drift_rate * FRACTIONAL(0.0001);
    aux_drift_rate = frac_mult(aux_drift_rate, FRACTIONAL(0.001));

    vol_ctrl_aux_channel_t  *aux_channel = &op_extra_data->aux_channel[aux_channel_index];

    /* Message shall be sent once aux path set up but before starting it,
     * if sent after that we aren't be able to meet the deadline for timed
     * playback so just return FALSE.
     */
    if(aux_channel->buffer != NULL &&
       aux_channel->state != AUX_STATE_NO_AUX &&
       aux_channel->state != AUX_STATE_END_AUX)
    {
        L2_DBG_MSG1("Volume Control Aux Timed Playback: "
                    "aux channel %d is already active",
                    aux_channel_index);
        return FALSE;
    }

    /* Sanity check on time to play time */
    TIME current_time = time_get_time();
    TIME_INTERVAL ttp_in_future = time_sub(aux_time_to_play, current_time);
    if(ttp_in_future < (VOL_CTRL_AUX_MIN_TTP_IN_FUTURE_MS*MILLISECOND) ||
       ttp_in_future > (VOL_CTRL_AUX_MAX_TTP_IN_FUTURE_MS*MILLISECOND))
    {
        L2_DBG_MSG2("Volume Control Aux Timed Playback: "
                    "Requested time to play is too early or too late,"
                    "current time=%d, time to play=%d",
                    current_time, aux_time_to_play);
        return FALSE;
    }

    /* Work out a gate time for aux input.
     * aux_time_to_play will be for first sample of aux,
     * we have a ramping down time for main channel before
     * actual mixing happens.
     * */
    vol_ctrl_aux_params_t *aux_param = (vol_ctrl_aux_params_t *)
        (&op_extra_data->parameters.OFFSET_AUX1_SCALE);
    unsigned atk_tc = aux_param[aux_channel_index].atk_tc;
    TIME_INTERVAL start_period = (TIME_INTERVAL) ((1<<(DAWTH-1))/atk_tc)*10;
    TIME ttp_gate_time = time_sub(aux_time_to_play, start_period);

    /* aux timed playback message in only for one tone/prompt,
     * if we don't receive the actual aux data within reasonable
     * time, timed playback will expire and later received aux data
     * will be mixed in normal non-ttp mode */
    TIME ttp_expiry_time = time_add(aux_time_to_play,
                                    VOL_CTRL_AUX_TTP_EXPIRY_PERIOD_MS*MILLISECOND);

    L2_DBG_MSG4("Volume Control, setting auxiliary playback time:"
                " time=%d, channel=%d, ttp=%d, drift=%d",
                time_get_time(), aux_channel_index, aux_time_to_play, aux_drift_rate);

    /* postpone processing just in case this update is preempted
     * by the operator's process. Normally user is expected to send
     * this message before auxiliary path starts sending audio data,
     * so practically perhaps not required.
     */
    opmgr_op_suspend_processing(op_data);
    op_extra_data->aux0_ttp.time_to_play = aux_time_to_play;
    op_extra_data->aux0_ttp.expiry_time = ttp_expiry_time;
    op_extra_data->aux0_ttp.gate_time = ttp_gate_time;
    op_extra_data->aux0_ttp.drift_rate = aux_drift_rate;
    op_extra_data->aux0_ttp.enabled = TRUE;

#ifdef VOLUME_CONTROL_AUX_TIMING_TRACE
    record_aux_ttr_event(op_data,
                         VOL_CTRL_TTR_EVENT_AUX_STATE,
                         pack_aux_state(op_extra_data),
                         aux_time_to_play);
#endif /* VOLUME_CONTROL_AUX_TIMING_TRACE */
    opmgr_op_resume_processing(op_data);
    return TRUE;
}

bool vol_ctlr_opmsg_set_downstream_latency_est(OPERATOR_DATA *op_data, void *message_data, unsigned *resp_length, OP_OPMSG_RSP_PAYLOAD **resp_data)
{
    VOL_CTRL_DATA_T *op_extra_data = get_instance_data(op_data);

    /* get downstream latency field */
    unsigned latency_est_ms = OPMSG_FIELD_GET(message_data,
                                              OPMSG_VOL_CTRL_SET_DOWNSTREAM_LATENCY_EST,
                                              LATENCY_EST_MS);
    /* check maximum value */
    if(latency_est_ms > VOL_CTRL_MAX_DOWNSTREAM_LATENCY_MS)
    {
        return FALSE;
    }

    /* set downstream latency config, change while
	   the operator is running is ok, but the value
	   will be used from next auxiliary burst.
	 */
    VOL_CTRL_DOWNSTREAM_LATENCY_EST(op_extra_data) = latency_est_ms;
    return TRUE;
}
#endif /* VOLUME_CONTROL_AUX_TTP_SUPPORT */
