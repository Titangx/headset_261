/*!
\copyright  Copyright (c) 2021 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\brief      Configuration used for the fixed output chain
*/

#include "kymera_output_fixed_chain_config.h"
#include "kymera_private.h"

#define FIXED_OUTPUT_RATE (48000)
#define FIXED_OUTPUT_KICK_PERIOD (KICK_PERIOD_SLOW)

static const kymera_output_chain_config chain_config =
{
    // The fixed output sample rate
    .rate = FIXED_OUTPUT_RATE,
    // Kick period when creating the chain
    .kick_period = FIXED_OUTPUT_KICK_PERIOD,

    // The size of the buffer at the input/output of the source sync in samples
    .source_sync_input_buffer_size_samples = DEFAULT_CODEC_BLOCK_SIZE + US_TO_BUFFER_SIZE_MONO_PCM(FIXED_OUTPUT_KICK_PERIOD, FIXED_OUTPUT_RATE) + 1,
    .source_sync_output_buffer_size_samples = US_TO_BUFFER_SIZE_MONO_PCM((5.0 * FIXED_OUTPUT_KICK_PERIOD) / 2.0, FIXED_OUTPUT_RATE),

    // The source sync min/max period
    .set_source_sync_min_period = TRUE,
    .source_sync_min_period = FRACTIONAL(1000.0 / FIXED_OUTPUT_KICK_PERIOD),
    .set_source_sync_max_period = TRUE,
    .source_sync_max_period = MILLISECONDS_Q6_26((1000.0 + FIXED_OUTPUT_KICK_PERIOD) / FIXED_OUTPUT_KICK_PERIOD),

    // The source sync kick back threshold
    .set_source_sync_kick_back_threshold = TRUE,
    .source_sync_kick_back_threshold = 256,

    .chain_include_aec = TRUE,
};

const kymera_output_chain_config * Kymera_GetFixedOutputChainConfig(void)
{
    return &chain_config;
}
