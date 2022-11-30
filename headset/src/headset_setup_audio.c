/*!
\copyright  Copyright (c) 2020 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version    %%version
\file       headset_setup_audio.c
\brief      Module Conifgure Audio chains for headset application
*/

#include "kymera.h"
#include "wired_audio_source.h"

#include "headset_cap_ids.h"
#include "headset_setup_audio.h"
#include "chains/chain_sco_nb.h"
#include "chains/chain_sco_wb.h"
#include "chains/chain_sco_swb.h"
#include "chains/chain_sco_nb_2mic.h"
#include "chains/chain_sco_wb_2mic.h"
#include "chains/chain_sco_swb_2mic.h"
#include "chains/chain_sco_nb_2mic_binaural.h"
#include "chains/chain_sco_wb_2mic_binaural.h"
#include "chains/chain_sco_swb_2mic_binaural.h"
#include "chains/chain_output_volume.h"
#include "chains/chain_output_volume_no_passthrough.h"
#include "chains/chain_prompt_decoder.h"
#include "chains/chain_prompt_decoder_no_iir.h"
#include "chains/chain_tone_gen.h"
#include "chains/chain_tone_gen_no_iir.h"
#include "chains/chain_aec.h"
#include "chains/chain_prompt_pcm.h"
#include "chains/chain_va_encode_msbc.h"
#include "chains/chain_va_encode_opus.h"
#include "chains/chain_va_encode_sbc.h"
#include "chains/chain_va_mic_1mic_cvc.h"
#include "chains/chain_va_mic_1mic_cvc_wuw.h"
#include "chains/chain_va_mic_2mic_cvc.h"
#include "chains/chain_va_mic_2mic_cvc_wuw.h"
#include "chains/chain_va_wuw_qva.h"
#include "chains/chain_va_wuw_gva.h"
#include "chains/chain_va_wuw_apva.h"

#include "chains/chain_input_sbc_stereo.h"
#include "chains/chain_input_aptx_stereo.h"
#include "chains/chain_input_aptxhd_stereo.h"
#include "chains/chain_input_aptx_adaptive_stereo.h"
#include "chains/chain_input_aptx_adaptive_stereo_q2q.h"
#include "chains/chain_input_aac_stereo.h"
#include "chains/chain_input_wired_analog_stereo.h"
#include "chains/chain_input_usb_stereo.h"
#include "chains/chain_usb_voice_rx_mono.h"
#include "chains/chain_usb_voice_rx_stereo.h"
#include "chains/chain_usb_voice_wb.h"
#include "chains/chain_usb_voice_wb_2mic.h"
#include "chains/chain_usb_voice_wb_2mic_binaural.h"
#include "chains/chain_usb_voice_nb.h"
#include "chains/chain_usb_voice_nb_2mic.h"
#include "chains/chain_usb_voice_nb_2mic_binaural.h"

#include "chains/chain_music_processing.h"
#ifdef INCLUDE_MUSIC_PROCESSING
        #include "chains/chain_music_processing_user_eq.h"
#endif

#include "chains/chain_mic_resampler.h"

#include "chains/chain_va_graph_manager.h"

static const capability_bundle_t capability_bundle[] =
{
#ifdef DOWNLOAD_AEC_REF
    {
        "download_aec_reference.edkcs",
        capability_bundle_available_p0
    },
#endif
#ifdef DOWNLOAD_VOLUME_CONTROL
    {
        "download_volume_control.edkcs",
        capability_bundle_available_p0
    },
#endif
#ifdef DOWNLOAD_VA_GRAPH_MANAGER
    {
        "download_va_graph_manager.edkcs",
        capability_bundle_available_p0
    },
#endif
#ifdef DOWNLOAD_CVC_FBC
    {
        "download_cvc_fbc.edkcs",
#ifdef __QCC514X__
        capability_bundle_available_p0_and_p1
#else
        capability_bundle_available_p0
#endif
    },
#endif
#ifdef DOWNLOAD_GVA
    {
#ifdef __QCC305X__
        "download_gva.edkcs",
#else
        "download_gva.dkcs",
#endif
        capability_bundle_available_p0
    },
#endif
#ifdef DOWNLOAD_APVA
    {
#ifdef __QCC305X__
        "download_apva.edkcs",
#else
        "download_apva.dkcs",
#endif
        capability_bundle_available_p0
    },
#endif
#ifdef DOWNLOAD_OPUS_CELT_ENCODE
    {
        "download_opus_celt_encode.edkcs",
        capability_bundle_available_p0
    },
#endif
#ifdef DOWNLOAD_CVC_FBC
    {
        "download_cvc_fbc.edkcs",
        capability_bundle_available_p0
    },
#endif
#ifdef DOWNLOAD_APTX_ADAPTIVE_DECODE
    {
        "download_aptx_adaptive_decode.edkcs",
        capability_bundle_available_p0
    },
#endif
#ifdef DOWNLOAD_SWITCHED_PASSTHROUGH
    {
        "download_switched_passthrough_consumer.edkcs",
        capability_bundle_available_p0
    },
#endif
#ifdef DOWNLOAD_SWBS
    {
        "download_swbs.edkcs",
        capability_bundle_available_p0
    },
#endif
    {
        0, 0
    }
};

static const capability_bundle_config_t bundle_config = {capability_bundle, ARRAY_DIM(capability_bundle) - 1};

static const kymera_chain_configs_t chain_configs = {
    .chain_sco_nb_config = &chain_sco_nb_config,
    .chain_sco_wb_config = &chain_sco_wb_config,
    .chain_sco_swb_config = &chain_sco_swb_config,
    .chain_sco_nb_2mic_config = &chain_sco_nb_2mic_config,
    .chain_sco_wb_2mic_config = &chain_sco_wb_2mic_config,
    .chain_sco_swb_2mic_config = &chain_sco_swb_2mic_config,
    .chain_sco_nb_2mic_binaural_config = &chain_sco_nb_2mic_binaural_config,
    .chain_sco_wb_2mic_binaural_config = &chain_sco_wb_2mic_binaural_config,
    .chain_sco_swb_2mic_binaural_config = &chain_sco_swb_2mic_binaural_config,
    .chain_output_volume_config = &chain_output_volume_config,
    .chain_output_volume_no_passthrough_config = &chain_output_volume_no_passthrough_config,
    .chain_tone_gen_config = &chain_tone_gen_config,
    .chain_tone_gen_no_iir_config = &chain_tone_gen_no_iir_config,
    .chain_aec_config = &chain_aec_config,
    .chain_prompt_pcm_config = &chain_prompt_pcm_config,
#ifdef INCLUDE_DECODERS_ON_P1
    .chain_input_sbc_stereo_config = &chain_input_sbc_stereo_config_p1,
    .chain_input_aptx_stereo_config = &chain_input_aptx_stereo_config_p1,
    .chain_input_aptxhd_stereo_config = &chain_input_aptxhd_stereo_config_p1,
    .chain_input_aac_stereo_config = &chain_input_aac_stereo_config_p1,
    .chain_input_aptx_adaptive_stereo_config = &chain_input_aptx_adaptive_stereo_config_p1,
    .chain_input_aptx_adaptive_stereo_q2q_config = &chain_input_aptx_adaptive_stereo_q2q_config_p1,
    .chain_prompt_decoder_config = &chain_prompt_decoder_config_p1,
    .chain_prompt_decoder_no_iir_config = &chain_prompt_decoder_no_iir_config_p1,
#else
#ifdef INCLUDE_WUW
#error Wake-up word requires decoders to be on P1
#endif
    .chain_input_sbc_stereo_config = &chain_input_sbc_stereo_config_p0,
    .chain_input_aptx_stereo_config = &chain_input_aptx_stereo_config_p0,
    .chain_input_aptxhd_stereo_config = &chain_input_aptxhd_stereo_config_p0,
    .chain_input_aac_stereo_config = &chain_input_aac_stereo_config_p0,
    .chain_prompt_decoder_config = &chain_prompt_decoder_config_p0,
    .chain_prompt_decoder_no_iir_config = &chain_prompt_decoder_no_iir_config_p0,
    .chain_input_aptx_adaptive_stereo_config = &chain_input_aptx_adaptive_stereo_config_p0,
    .chain_input_aptx_adaptive_stereo_q2q_config = &chain_input_aptx_adaptive_stereo_q2q_config_p0,
#endif

#ifdef INCLUDE_SPEAKER_EQ
#ifdef INCLUDE_DECODERS_ON_P1
#ifdef INCLUDE_MUSIC_PROCESSING
    .chain_music_processing_config = &chain_music_processing_user_eq_config_p1,
#else
    .chain_music_processing_config = &chain_music_processing_config_p1,
#endif /* INCLUDE_MUSIC_PROCESSING */
#else
#ifdef INCLUDE_MUSIC_PROCESSING
    .chain_music_processing_config = &chain_music_processing_user_eq_config_p0,
#else
    .chain_music_processing_config = &chain_music_processing_config_p0,
#endif /* INCLUDE_MUSIC_PROCESSING */
#endif /* INCLUDE_DECODERS_ON_P1 */
#endif /* INCLUDE_SPEAKER_EQ */

    .chain_input_wired_analog_stereo_config = &chain_input_wired_analog_stereo_config,
    .chain_input_usb_stereo_config = &chain_input_usb_stereo_config,
    .chain_usb_voice_rx_mono_config = &chain_usb_voice_rx_mono_config,
    .chain_usb_voice_rx_stereo_config = &chain_usb_voice_rx_stereo_config,
    .chain_usb_voice_wb_config = &chain_usb_voice_wb_config,
    .chain_usb_voice_wb_2mic_config = &chain_usb_voice_wb_2mic_config,
    .chain_usb_voice_wb_2mic_binaural_config = &chain_usb_voice_wb_2mic_binaural_config,
    .chain_usb_voice_nb_config = &chain_usb_voice_nb_config,
    .chain_usb_voice_nb_2mic_config = &chain_usb_voice_nb_2mic_config,
    .chain_usb_voice_nb_2mic_binaural_config = &chain_usb_voice_nb_2mic_binaural_config,
#ifdef INCLUDE_MIC_CONCURRENCY
    .chain_mic_resampler_config = &chain_mic_resampler_config,
#endif
    .chain_va_graph_manager_config = &chain_va_graph_manager_config,
};

const appKymeraVaEncodeChainInfo va_encode_chain_info[] =
{
    {{va_audio_codec_sbc}, &chain_va_encode_sbc_config},
    {{va_audio_codec_msbc}, &chain_va_encode_msbc_config},
    {{va_audio_codec_opus}, &chain_va_encode_opus_config}
};

static const appKymeraVaEncodeChainTable va_encode_chain_table =
{
    .chain_table = va_encode_chain_info,
    .table_length = ARRAY_DIM(va_encode_chain_info)
};

static const appKymeraVaMicChainInfo va_mic_chain_info[] =
{
  /*{{  WuW,   CVC, mics}, chain_to_use}*/
#ifdef INCLUDE_WUW
    {{ TRUE,  TRUE,    1}, &chain_va_mic_1mic_cvc_wuw_config},
    {{ TRUE,  TRUE,    2}, &chain_va_mic_2mic_cvc_wuw_config},
#endif /* INCLUDE_WUW */
    {{FALSE,  TRUE,    1}, &chain_va_mic_1mic_cvc_config},
    {{FALSE,  TRUE,    2}, &chain_va_mic_2mic_cvc_config}
};

static const appKymeraVaMicChainTable va_mic_chain_table =
{
    .chain_table = va_mic_chain_info,
    .table_length = ARRAY_DIM(va_mic_chain_info)
};

#ifdef INCLUDE_WUW
static const appKymeraVaWuwChainInfo va_wuw_chain_info[] =
{
    {{va_wuw_engine_qva}, &chain_va_wuw_qva_config},
#ifdef INCLUDE_GAA
    {{va_wuw_engine_gva}, &chain_va_wuw_gva_config},
#endif /* INCLUDE_GAA */
#ifdef INCLUDE_AMA
    {{va_wuw_engine_apva}, &chain_va_wuw_apva_config}
#endif /* INCLUDE_AMA */
};

static const appKymeraVaWuwChainTable va_wuw_chain_table =
{
    .chain_table = va_wuw_chain_info,
    .table_length = ARRAY_DIM(va_wuw_chain_info)
};
#endif /* INCLUDE_WUW */

const appKymeraScoChainInfo kymera_sco_chain_table[] =
{
#ifdef KYMERA_SCO_USE_2MIC
#ifdef KYMERA_SCO_USE_2MIC_BINAURAL

    /* 2-mic cVc Binaural configurations
     sco_mode  sco_fwd   mic_fwd   mic_cfg   chain                             rate */
    { SCO_NB,   FALSE,    FALSE,    2,    &chain_sco_nb_2mic_binaural_config,  8000 },
    { SCO_WB,   FALSE,    FALSE,    2,    &chain_sco_wb_2mic_binaural_config, 16000 },
    { SCO_SWB,  FALSE,    FALSE,    2,    &chain_sco_swb_2mic_binaural_config, 32000 },
#else  /* KYMERA_SCO_USE_2MIC_BINAURAL */

    /* 2-mic cVc Endfire configurations
     sco_mode  sco_fwd   mic_fwd   mic_cfg   chain                      rate */
    { SCO_NB,  FALSE,    FALSE,    2,       &chain_sco_nb_2mic_config,  8000 },
    { SCO_WB,  FALSE,    FALSE,    2,       &chain_sco_wb_2mic_config,  16000 },
    { SCO_SWB, FALSE,    FALSE,    2,       &chain_sco_swb_2mic_config, 32000 },
#endif /* KYMERA_SCO_USE_2MIC_BINAURAL */
#else   /* KYMERA_SCO_USE_2MIC */

    /* 1-mic CVC configurations
    sco_mode  sco_fwd   mic_fwd   mic_cfg   chain                  rate */
    { SCO_NB,  FALSE,    FALSE,     1,      &chain_sco_nb_config,  8000 },
    { SCO_WB,  FALSE,    FALSE,     1,      &chain_sco_wb_config,  16000 },
    { SCO_SWB, FALSE,    FALSE,     1,      &chain_sco_swb_config, 32000 },
#endif /* KYMERA_SCO_USE_2MIC */

    { NO_SCO }
};

const wired_audio_config_t wired_audio_config =
{
    .rate = 48000,
    .min_latency = 10,/*! in milli-seconds */
    .max_latency = 40,/*! in milli-seconds */
    .target_latency = 30 /*! in milli-seconds */
};

const audio_output_config_t audio_hw_output_config =
{
#ifdef ENABLE_I2S_OUTPUT
    .mapping = {
        {audio_output_type_i2s, audio_output_hardware_instance_0, audio_output_channel_a },
        {audio_output_type_i2s, audio_output_hardware_instance_0, audio_output_channel_b }
    },
    .gain_type = {audio_output_gain_digital, audio_output_gain_digital},
    .output_resolution_mode = audio_output_16_bit,
    .fixed_hw_gain = -1440 /* -24dB */
#else
    .mapping = {
        {audio_output_type_dac, audio_output_hardware_instance_0, audio_output_channel_a },
        {audio_output_type_dac, audio_output_hardware_instance_0, audio_output_channel_b }
    },
    .gain_type = {audio_output_gain_none, audio_output_gain_none},
    .output_resolution_mode = audio_output_24_bit,
    .fixed_hw_gain = 0
#endif
};

bool headsetSetupAudio(void)
{
    DEBUG_LOG("headsetSetupAudio");
    Kymera_SetChainConfigs(&chain_configs);
    Kymera_SetScoChainTable(kymera_sco_chain_table);
    WiredAudioSource_Configure(&wired_audio_config);
    Kymera_SetVaMicChainTable(&va_mic_chain_table);
    Kymera_SetVaEncodeChainTable(&va_encode_chain_table);
#ifdef INCLUDE_WUW
    Kymera_SetVaWuwChainTable(&va_wuw_chain_table);
    Kymera_StoreLargestWuwEngine();
#endif /* INCLUDE_WUW */
    AudioOutputInit(&audio_hw_output_config);
    return TRUE;
}

void headsetSetBundlesConfig(void)
{
    Kymera_SetBundleConfig(&bundle_config);
}

