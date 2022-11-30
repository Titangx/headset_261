/*******************************************************************************
Copyright (c) 2017-2020 Qualcomm Technologies International, Ltd.


FILE NAME
    anc_configure_coefficients.c

DESCRIPTION
    Functions required to configure the IIR/LP/DC filters and gains.
*/

#include "anc_configure_coefficients.h"
#include "anc_config_data.h"
#include "anc_data.h"
#include "anc_debug.h"
#include <audio_anc.h>
#include <source.h>

#define DAWTH 32
/* Convert x into 1.(DAWTH - 1) format */
#define FRACTIONAL(x) ( (int)( (x) * ((1l<<(DAWTH-1)) - 1) ))


#define ACCMD_ANC_CONTROL_OUTMIX_EN_MASK               0x0040
#define ACCMD_ANC_CONTROL_ACCESS_SELECT_ENABLES_SHIFT  16

#define ANC_MUTE_GAIN  0

/*
 * \brief Inline assembly helper function for performing a fractional multiplication
 *
 * \param a The first value for the multiplication in fractional encoding
 * \param b The value to multiply a by in fractional encoding
 * \return Multiplication result in fractional encoding.
 */
#if defined(__KALIMBA__) && !defined(__GNUC__)
asm int frac_mult(int a, int b)
{
    @{} = @{a} * @{b} (frac);
}
#else
#define frac_mult(a, b) ((int)((((long long)a)*(b)) >> (DAWTH-1))) 
#endif

static uint16 convertCoefficientFromStoredFormat(uint32 coefficient)
{
    return frac_mult(coefficient,FRACTIONAL(1.0/16.0));
}

static void readCoefficients(uint16 * req_coefficient, iir_config_t * iir_config)
{
    unsigned index;
    for(index = 0; index < NUMBER_OF_IIR_COEFFICIENTS; index++)
    {
        req_coefficient[index] = convertCoefficientFromStoredFormat(iir_config->coefficients[index]);
    }
}


static void setIirCoefficients(audio_anc_instance instance, anc_instance_config_t * config)
{
    uint16 coefficients[NUMBER_OF_IIR_COEFFICIENTS];

    readCoefficients(coefficients, &config->feed_forward_a.iir_config);
    ANC_ASSERT(AudioAncFilterIirSet(instance, AUDIO_ANC_PATH_ID_FFA, NUMBER_OF_IIR_COEFFICIENTS, coefficients));

    memset(coefficients, 0, sizeof(uint16)*NUMBER_OF_IIR_COEFFICIENTS);
    readCoefficients(coefficients, &config->feed_forward_b.iir_config);
    ANC_ASSERT(AudioAncFilterIirSet(instance, AUDIO_ANC_PATH_ID_FFB, NUMBER_OF_IIR_COEFFICIENTS, coefficients));

    memset(coefficients, 0, sizeof(uint16)*NUMBER_OF_IIR_COEFFICIENTS);
    readCoefficients(coefficients, &config->feed_back.iir_config);
    ANC_ASSERT(AudioAncFilterIirSet(instance, AUDIO_ANC_PATH_ID_FB, NUMBER_OF_IIR_COEFFICIENTS,coefficients));
}

static void setLpfCoefficients(audio_anc_instance instance, anc_instance_config_t * config)
{
    ANC_ASSERT(AudioAncFilterLpfSet(instance, AUDIO_ANC_PATH_ID_FFA,
                    config->feed_forward_a.lpf_config.lpf_shift1,
                    config->feed_forward_a.lpf_config.lpf_shift2));

    ANC_ASSERT(AudioAncFilterLpfSet(instance, AUDIO_ANC_PATH_ID_FFB,
                        config->feed_forward_b.lpf_config.lpf_shift1,
                        config->feed_forward_b.lpf_config.lpf_shift2));

    ANC_ASSERT(AudioAncFilterLpfSet(instance, AUDIO_ANC_PATH_ID_FB,
                        config->feed_back.lpf_config.lpf_shift1,
                        config->feed_back.lpf_config.lpf_shift2));
}
/******************************************************************************/
static anc_path_enable getAncMicPathForSingleFilterTopology(audio_anc_instance instance)
{
    anc_path_enable mic_path = feed_forward_left;

    if(instance == AUDIO_ANC_INSTANCE_0)
    {
        switch(ancDataGetMicParams()->enabled_mics)
        {
            case feed_forward_mode:
            case feed_forward_mode_left_only:
            case hybrid_mode:
            case hybrid_mode_left_only:
                mic_path = feed_forward_left;
                break;
            case feed_back_mode:
            case feed_back_mode_left_only:
                mic_path = feed_back_left;
                break;
            default:
                Panic();
                break;
        }
    }
    else
    {
        switch(ancDataGetMicParams()->enabled_mics)
        {
            case feed_forward_mode:
            case feed_forward_mode_right_only:
            case hybrid_mode:
            case hybrid_mode_right_only:
                mic_path = feed_forward_right;
                break;
            case feed_back_mode:
            case feed_back_mode_right_only:
                mic_path = feed_back_right;
                break;
            default:
                Panic();
                break;
        }
    }

    return mic_path;
}
/******************************************************************************/
static anc_path_enable getAncMicPathForParallelFilterTopology(audio_anc_instance instance)
{
    anc_path_enable mic_path = feed_forward_left;

    switch(ancDataGetMicParams()->enabled_mics)
    {
        case feed_forward_mode_left_only:
            mic_path = feed_forward_left;
            break;

        case feed_back_mode_left_only:
            mic_path = feed_back_left;
            break;

        case hybrid_mode_left_only:
            mic_path = (instance == AUDIO_ANC_INSTANCE_0) ? feed_forward_left : feed_back_left;
            break;

        default:
            break;
    }

    return mic_path;
}

/******************************************************************************/
static Source getAnyAncMicSourceForInstance(audio_anc_instance instance)
{
    anc_path_enable mic_path = feed_forward_left;
    audio_mic_params * mic_params = NULL;

    if(ancDataGetTopology()==anc_parallel_filter_topology)
    {
        mic_path=getAncMicPathForParallelFilterTopology(instance);
    }
    else
    {
        mic_path=getAncMicPathForSingleFilterTopology(instance);
    }

    mic_params = ancConfigDataGetMicForMicPath(mic_path);

    return AudioPluginGetMicSource(*mic_params, mic_params->channel);
}

static void setDcFilters(Source mic_source, anc_instance_config_t * config)
{

    ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FFA_DC_FILTER_SHIFT, config->feed_forward_a.dc_filter_config.filter_shift));
    ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FFA_DC_FILTER_ENABLE,config->feed_forward_a.dc_filter_config.filter_enable));

    ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FFB_DC_FILTER_SHIFT, config->feed_forward_b.dc_filter_config.filter_shift));
    ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FFB_DC_FILTER_ENABLE,config->feed_forward_b.dc_filter_config.filter_enable));
}

static void setSmallLpf(Source mic_source, anc_instance_config_t * config)
{

    ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_SM_LPF_FILTER_SHIFT, config->small_lpf.small_lpf_config.filter_shift));
    ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_SM_LPF_FILTER_ENABLE,config->small_lpf.small_lpf_config.filter_enable));
}


static void setPathGains(Source mic_source, anc_instance_config_t * config)
{
    ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FFA_GAIN_SHIFT, config->feed_forward_a.gain_config.gain_shift));
    ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FFA_GAIN, config->feed_forward_a.gain_config.gain));

    ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FFB_GAIN_SHIFT, config->feed_forward_b.gain_config.gain_shift));
    ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FFB_GAIN, config->feed_forward_b.gain_config.gain));

    ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FB_GAIN_SHIFT, config->feed_back.gain_config.gain_shift));
    ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FB_GAIN, config->feed_back.gain_config.gain));
}

static void setPathGainsParallelAncHybridMode(void)
{
    Source ffb_mic_source, ffa_mic_source;

    anc_instance_config_t * config_instance_0 = getInstanceConfig(AUDIO_ANC_INSTANCE_0);
    anc_instance_config_t * config_instance_1 = getInstanceConfig(AUDIO_ANC_INSTANCE_1);

    ffb_mic_source = getAnyAncMicSourceForInstance(AUDIO_ANC_INSTANCE_0);
    ffa_mic_source = getAnyAncMicSourceForInstance(AUDIO_ANC_INSTANCE_1);

    ANC_ASSERT(SourceConfigure(ffb_mic_source, STREAM_ANC_FFA_GAIN_SHIFT, config_instance_0->feed_forward_a.gain_config.gain_shift));
    ANC_ASSERT(SourceConfigure(ffa_mic_source, STREAM_ANC_FFA_GAIN_SHIFT, config_instance_1->feed_forward_a.gain_config.gain_shift));
    ANC_ASSERT(SourceConfigure(ffb_mic_source, STREAM_ANC_FFA_GAIN, config_instance_0->feed_forward_a.gain_config.gain));
    ANC_ASSERT(SourceConfigure(ffa_mic_source, STREAM_ANC_FFA_GAIN, config_instance_1->feed_forward_a.gain_config.gain));

    ANC_ASSERT(SourceConfigure(ffb_mic_source, STREAM_ANC_FFB_GAIN_SHIFT, config_instance_0->feed_forward_b.gain_config.gain_shift));
    ANC_ASSERT(SourceConfigure(ffa_mic_source, STREAM_ANC_FFB_GAIN_SHIFT, config_instance_1->feed_forward_b.gain_config.gain_shift));
    ANC_ASSERT(SourceConfigure(ffb_mic_source, STREAM_ANC_FFB_GAIN, config_instance_0->feed_forward_b.gain_config.gain));
    ANC_ASSERT(SourceConfigure(ffa_mic_source, STREAM_ANC_FFB_GAIN, config_instance_1->feed_forward_b.gain_config.gain));

    ANC_ASSERT(SourceConfigure(ffb_mic_source, STREAM_ANC_FB_GAIN_SHIFT, config_instance_0->feed_back.gain_config.gain_shift));
    ANC_ASSERT(SourceConfigure(ffa_mic_source, STREAM_ANC_FB_GAIN_SHIFT, config_instance_1->feed_back.gain_config.gain_shift));
    ANC_ASSERT(SourceConfigure(ffb_mic_source, STREAM_ANC_FB_GAIN, config_instance_0->feed_back.gain_config.gain));
    ANC_ASSERT(SourceConfigure(ffa_mic_source, STREAM_ANC_FB_GAIN, config_instance_1->feed_back.gain_config.gain));

}

static void ancConfigureParallelFilterHardwareConfigKey(stream_config_key config_key, uint32 inst0_value, uint32 inst1_value)
{
    Source ffx_mic_source = getAnyAncMicSourceForInstance(AUDIO_ANC_INSTANCE_0);


    /* If same value needs to be configured for both instances, single trap would be sufficient
       Else, 2 trap calls would be used for configuring both instances*/
    if(inst0_value == inst1_value)
    {
        ANC_ASSERT(SourceConfigure(ffx_mic_source, config_key, inst0_value|INSTANCE_01_MASK));
    }
    else
    {
        ANC_ASSERT(SourceConfigure(ffx_mic_source, config_key, inst0_value|INSTANCE_0_MASK));
        ANC_ASSERT(SourceConfigure(ffx_mic_source, config_key, inst1_value|INSTANCE_1_MASK));
    }
}

static void setPathGainsParallelAncFFMode(void)
{
    anc_instance_config_t * config_0 = getInstanceConfig(AUDIO_ANC_INSTANCE_0);
    anc_instance_config_t * config_1 = getInstanceConfig(AUDIO_ANC_INSTANCE_1);

    ancConfigureParallelFilterHardwareConfigKey(STREAM_ANC_FFA_GAIN_SHIFT, config_0->feed_forward_a.gain_config.gain_shift, config_1->feed_forward_a.gain_config.gain_shift);
    ancConfigureParallelFilterHardwareConfigKey(STREAM_ANC_FFA_GAIN, config_0->feed_forward_a.gain_config.gain, config_1->feed_forward_a.gain_config.gain);
}

static void setPathGainsParallelAncFBMode(void)
{
    anc_instance_config_t * config_0 = getInstanceConfig(AUDIO_ANC_INSTANCE_0);
    anc_instance_config_t * config_1 = getInstanceConfig(AUDIO_ANC_INSTANCE_1);

    ancConfigureParallelFilterHardwareConfigKey(STREAM_ANC_FFA_GAIN_SHIFT, config_0->feed_forward_a.gain_config.gain_shift, config_1->feed_forward_a.gain_config.gain_shift);
    ancConfigureParallelFilterHardwareConfigKey(STREAM_ANC_FFA_GAIN, config_0->feed_forward_a.gain_config.gain, config_1->feed_forward_a.gain_config.gain);

    ancConfigureParallelFilterHardwareConfigKey(STREAM_ANC_FB_GAIN_SHIFT, config_0->feed_back.gain_config.gain_shift, config_1->feed_back.gain_config.gain_shift);
    ancConfigureParallelFilterHardwareConfigKey(STREAM_ANC_FB_GAIN, config_0->feed_back.gain_config.gain, config_1->feed_back.gain_config.gain);
}

static void configureCoefficientForInstance(audio_anc_instance instance)
{
    anc_instance_config_t * config = getInstanceConfig(instance);

    Source mic_source = getAnyAncMicSourceForInstance(instance);

    setIirCoefficients(instance, config);
    setLpfCoefficients(instance, config);

    setDcFilters(mic_source, config);
    setSmallLpf(mic_source, config);
}

static void setIirCoefficientsForFFAPath(audio_anc_instance instance)
{
    anc_instance_config_t * config = getInstanceConfig(instance);
    uint16 coefficients[NUMBER_OF_IIR_COEFFICIENTS];

    readCoefficients(coefficients, &config->feed_forward_a.iir_config);
    ANC_ASSERT(AudioAncFilterIirSet(instance, AUDIO_ANC_PATH_ID_FFA, NUMBER_OF_IIR_COEFFICIENTS, coefficients));
}

static void setIirCoefficientsForFBPath(audio_anc_instance instance)
{
    anc_instance_config_t * config = getInstanceConfig(instance);
    uint16 coefficients[NUMBER_OF_IIR_COEFFICIENTS];

    readCoefficients(coefficients, &config->feed_back.iir_config);
    ANC_ASSERT(AudioAncFilterIirSet(instance, AUDIO_ANC_PATH_ID_FB, NUMBER_OF_IIR_COEFFICIENTS,coefficients));
}

static void setIirCoefficientsForParallelFilterFFAPath(void)
{
    setIirCoefficientsForFFAPath(AUDIO_ANC_INSTANCE_0);
    setIirCoefficientsForFFAPath(AUDIO_ANC_INSTANCE_1);
}

static void setIirCoefficientsForParallelFilterFBPath(void)
{
    setIirCoefficientsForFBPath(AUDIO_ANC_INSTANCE_0);
    setIirCoefficientsForFBPath(AUDIO_ANC_INSTANCE_1);
}

static void setLpfCoefficientsForFFAPath(audio_anc_instance instance)
{
    anc_instance_config_t * config = getInstanceConfig(instance);

    ANC_ASSERT(AudioAncFilterLpfSet(instance, AUDIO_ANC_PATH_ID_FFA,
                    config->feed_forward_a.lpf_config.lpf_shift1,
                    config->feed_forward_a.lpf_config.lpf_shift2));
}

static void setLpfCoefficientsForFBPath(audio_anc_instance instance)
{
    anc_instance_config_t * config = getInstanceConfig(instance);

    ANC_ASSERT(AudioAncFilterLpfSet(instance, AUDIO_ANC_PATH_ID_FB,
                        config->feed_back.lpf_config.lpf_shift1,
                        config->feed_back.lpf_config.lpf_shift2));
}

static void setLpfCoefficientsForParallelFilterFFAPath(void)
{
    setLpfCoefficientsForFFAPath(AUDIO_ANC_INSTANCE_0);
    setLpfCoefficientsForFFAPath(AUDIO_ANC_INSTANCE_1);
}

static void setLpfCoefficientsForParallelFilterFBPath(void)
{
    setLpfCoefficientsForFBPath(AUDIO_ANC_INSTANCE_0);
    setLpfCoefficientsForFBPath(AUDIO_ANC_INSTANCE_1);
}

static void configureParallelFilterCoefficientsForFFAPath(void)
{
    setIirCoefficientsForParallelFilterFFAPath();
    setLpfCoefficientsForParallelFilterFFAPath();
}

static void configureParallelFilterCoefficientsForFBPath(void)
{
    setIirCoefficientsForParallelFilterFBPath();
    setLpfCoefficientsForParallelFilterFBPath();
}

static void setDcFiltersForParallelFilterFFAPath(void)
{
    anc_instance_config_t * config_0 = getInstanceConfig(AUDIO_ANC_INSTANCE_0);
    anc_instance_config_t * config_1 = getInstanceConfig(AUDIO_ANC_INSTANCE_1);

    ancConfigureParallelFilterHardwareConfigKey(STREAM_ANC_FFA_DC_FILTER_SHIFT, config_0->feed_forward_a.dc_filter_config.filter_shift, config_1->feed_forward_a.dc_filter_config.filter_shift);
    ancConfigureParallelFilterHardwareConfigKey(STREAM_ANC_FFA_DC_FILTER_ENABLE, config_0->feed_forward_a.dc_filter_config.filter_enable, config_1->feed_forward_a.dc_filter_config.filter_enable);
}

static void setSmallLpfForParallelFilterTopology(void)
{
    anc_instance_config_t * config_0 = getInstanceConfig(AUDIO_ANC_INSTANCE_0);
    anc_instance_config_t * config_1 = getInstanceConfig(AUDIO_ANC_INSTANCE_1);

    ancConfigureParallelFilterHardwareConfigKey(STREAM_ANC_SM_LPF_FILTER_SHIFT, config_0->small_lpf.small_lpf_config.filter_shift, config_1->small_lpf.small_lpf_config.filter_shift);
    ancConfigureParallelFilterHardwareConfigKey(STREAM_ANC_SM_LPF_FILTER_ENABLE, config_0->small_lpf.small_lpf_config.filter_enable, config_1->small_lpf.small_lpf_config.filter_enable);
}

static void configureFiltersForParallelFilterFFMode(void)
{
    setDcFiltersForParallelFilterFFAPath();
    setSmallLpfForParallelFilterTopology();
}

static void configureFiltersForParallelFilterFBMode(void)
{
    setDcFiltersForParallelFilterFFAPath();
    setSmallLpfForParallelFilterTopology();
}

static void configureGainsForInstance(audio_anc_instance instance)
{
    anc_instance_config_t * config = getInstanceConfig(instance);

    Source mic_source = getAnyAncMicSourceForInstance(instance);

    setPathGains(mic_source, config);
}

static void ancConfigureParallelCoeficientsForHybridMode(void)
{
    configureCoefficientForInstance(AUDIO_ANC_INSTANCE_0);
    configureCoefficientForInstance(AUDIO_ANC_INSTANCE_1);
}

static void ancConfigureParallelCoeficientsForFFMode(void)
{
    configureParallelFilterCoefficientsForFFAPath();
    configureFiltersForParallelFilterFFMode();
}

static void ancConfigureParallelCoeficientsForFBMode(void)
{
    configureParallelFilterCoefficientsForFFAPath();
    configureParallelFilterCoefficientsForFBPath();
    configureFiltersForParallelFilterFBMode();
}

static void ancSetParallelGainForFFApathUsingInstanceMask(uint8 instance_0_gain, uint8 instance_1_gain)
{
    ancConfigureParallelFilterHardwareConfigKey(STREAM_ANC_FFA_GAIN, instance_0_gain, instance_1_gain);
}

static void ancSetParallelGainForFFApath(uint8 instance_0_gain, uint8 instance_1_gain)
{
    Source ffb_mic_source, ffa_mic_source;

    ffb_mic_source = getAnyAncMicSourceForInstance(AUDIO_ANC_INSTANCE_0);
    ffa_mic_source = getAnyAncMicSourceForInstance(AUDIO_ANC_INSTANCE_1);

    ANC_ASSERT(SourceConfigure(ffb_mic_source, STREAM_ANC_FFA_GAIN, instance_0_gain));
    ANC_ASSERT(SourceConfigure(ffa_mic_source, STREAM_ANC_FFA_GAIN, instance_1_gain));
}

static void ancSetParallelGainForFFBpathUsingInstanceMask(uint8 instance_0_gain, uint8 instance_1_gain)
{
    ancConfigureParallelFilterHardwareConfigKey(STREAM_ANC_FFB_GAIN, instance_0_gain, instance_1_gain);
}

static void ancSetParallelGainForFFBpath(uint8 instance_0_gain, uint8 instance_1_gain)
{
    Source ffb_mic_source, ffa_mic_source;

    ffb_mic_source = getAnyAncMicSourceForInstance(AUDIO_ANC_INSTANCE_0);
    ffa_mic_source = getAnyAncMicSourceForInstance(AUDIO_ANC_INSTANCE_1);


    ANC_ASSERT(SourceConfigure(ffb_mic_source, STREAM_ANC_FFB_GAIN, instance_0_gain));
    ANC_ASSERT(SourceConfigure(ffa_mic_source, STREAM_ANC_FFB_GAIN, instance_1_gain));
}

static void ancSetParallelGainForFBpathUsingInstanceMask(uint8 instance_0_gain, uint8 instance_1_gain)
{
    ancConfigureParallelFilterHardwareConfigKey(STREAM_ANC_FB_GAIN, instance_0_gain, instance_1_gain);
}

static void ancSetParallelGainForFBpath(uint8 instance_0_gain, uint8 instance_1_gain)
{
    Source ffb_mic_source, ffa_mic_source;

    ffb_mic_source = getAnyAncMicSourceForInstance(AUDIO_ANC_INSTANCE_0);
    ffa_mic_source = getAnyAncMicSourceForInstance(AUDIO_ANC_INSTANCE_1);

    ANC_ASSERT(SourceConfigure(ffb_mic_source, STREAM_ANC_FB_GAIN, instance_0_gain));
    ANC_ASSERT(SourceConfigure(ffa_mic_source, STREAM_ANC_FB_GAIN, instance_1_gain));
}

/******************************************************************************/
static void setUserPathGains(Source mic_source, anc_user_gain_config_t * config)
{
    if(config)
    {
        if(config->enable_ffa_gain_update)
           ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FFA_GAIN, (unsigned)(config->ffa_gain & 0xFF)));

        if(config->enable_ffb_gain_update)
           ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FFB_GAIN, (unsigned)(config->ffb_gain & 0xFF)));

        if(config->enable_fb_gain_update)
           ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FB_GAIN, (unsigned)(config->fb_gain & 0xFF)));

        if(config->enable_ffa_gain_shift_update)
           ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FFA_GAIN_SHIFT, (unsigned)(config->ffa_gain_shift)));

        if(config->enable_ffb_gain_shift_update)
           ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FFB_GAIN_SHIFT, (unsigned)(config->ffb_gain_shift)));

        if(config->enable_fb_gain_shift_update)
           ANC_ASSERT(SourceConfigure(mic_source, STREAM_ANC_FB_GAIN_SHIFT, (unsigned)(config->fb_gain_shift)));
    }
}

uint32 getAncInstanceMask(anc_instance_mask_t instance_mask)
{
    uint32 result_mask;

    switch(instance_mask)
    {
        case ANC_INSTANCE_EP_BASED:
            result_mask= 0;
        break;

        case ANC_INSTANCE_0_MASK:
            result_mask= INSTANCE_0_MASK;
        break;

        case ANC_INSTANCE_1_MASK:
            result_mask= INSTANCE_1_MASK;
        break;

        default:
            result_mask= 0;
        break;
    }
    return result_mask;
}



/******************************************************************************/
anc_instance_config_t * getInstanceConfig(audio_anc_instance instance)
{
    return (instance == AUDIO_ANC_INSTANCE_1 ? &ancDataGetCurrentModeConfig()->instance[ANC_INSTANCE_1_INDEX]
                                                : &ancDataGetCurrentModeConfig()->instance[ANC_INSTANCE_0_INDEX]);
}
/******************************************************************************/
void ancConfigureMutePathGains(void)
{
    if(ancDataIsLeftChannelConfigurable())
    {
       Source mic_source_left = getAnyAncMicSourceForInstance(AUDIO_ANC_INSTANCE_0);
       
       if(mic_source_left)
       {
          ANC_ASSERT(SourceConfigure(mic_source_left, STREAM_ANC_FFA_GAIN, 0));
          ANC_ASSERT(SourceConfigure(mic_source_left, STREAM_ANC_FFB_GAIN, 0));
          ANC_ASSERT(SourceConfigure(mic_source_left, STREAM_ANC_FB_GAIN, 0));
       }
    }

    if(ancDataIsRightChannelConfigurable())
    {
       Source mic_source_right = getAnyAncMicSourceForInstance(AUDIO_ANC_INSTANCE_1);

       if(mic_source_right)
       {
          ANC_ASSERT(SourceConfigure(mic_source_right, STREAM_ANC_FFA_GAIN, 0));
          ANC_ASSERT(SourceConfigure(mic_source_right, STREAM_ANC_FFB_GAIN, 0));
          ANC_ASSERT(SourceConfigure(mic_source_right, STREAM_ANC_FB_GAIN, 0));
       }
    }
}

/******************************************************************************/
void ancConfigureFilterCoefficients(void)
{
    if(ancDataIsLeftChannelConfigurable())
    {
        configureCoefficientForInstance(AUDIO_ANC_INSTANCE_0);
    }

    if(ancDataIsRightChannelConfigurable())
    {
        configureCoefficientForInstance(AUDIO_ANC_INSTANCE_1);
    }
}

/******************************************************************************/
void ancConfigureFilterPathGains(void)
{
    if(ancDataIsLeftChannelConfigurable())
    {
        configureGainsForInstance(AUDIO_ANC_INSTANCE_0);
    }

    if(ancDataIsRightChannelConfigurable())
    {
        configureGainsForInstance(AUDIO_ANC_INSTANCE_1);
    }
}

/******************************************************************************/
bool ancConfigureGainForFFApath(audio_anc_instance instance, uint8 gain)
{
    Source mic_source = getAnyAncMicSourceForInstance(instance);

    if(mic_source)
    {
        return(SourceConfigure(mic_source, STREAM_ANC_FFA_GAIN, gain));
    }
    return FALSE;
}

/******************************************************************************/
bool ancConfigureGainForFFBpath(audio_anc_instance instance, uint8 gain)
{
    Source mic_source = getAnyAncMicSourceForInstance(instance);

    if(mic_source)
    {
        return(SourceConfigure(mic_source, STREAM_ANC_FFB_GAIN,gain));
    }
    return FALSE;
}

/******************************************************************************/
bool ancConfigureGainForFBpath(audio_anc_instance instance, uint8 gain)
{
    Source mic_source = getAnyAncMicSourceForInstance(instance);
    if(mic_source)
    {
        return(SourceConfigure(mic_source, STREAM_ANC_FB_GAIN,gain));
    }
    return FALSE;
}
/******************************************************************************/
bool ancConfigureParallelGainForFFApath(uint8 instance_0_gain, uint8 instance_1_gain)
{
    bool status=FALSE;

    switch(ancDataGetMicParams()->enabled_mics)
    {
        case feed_forward_mode_left_only:
        case feed_back_mode_left_only:
            ancSetParallelGainForFFApathUsingInstanceMask(instance_0_gain,instance_1_gain);
            status= TRUE;
            break;

        case hybrid_mode_left_only:
            ancSetParallelGainForFFApath(instance_0_gain,instance_1_gain);
            status= TRUE;
            break;

        default:
            ANC_DEBUG_INFO("Unsupported in enhanced ANC\n");
            ANC_PANIC();
            break;
    }

    return status;
}

/******************************************************************************/
bool ancConfigureParallelGainForFFBpath(uint8 instance_0_gain, uint8 instance_1_gain)
{
    bool status=FALSE;

    switch(ancDataGetMicParams()->enabled_mics)
    {
        case feed_forward_mode_left_only:
        case feed_back_mode_left_only:
            ancSetParallelGainForFFBpathUsingInstanceMask(instance_0_gain,instance_1_gain);
            status= TRUE;
            break;

        case hybrid_mode_left_only:
            ancSetParallelGainForFFBpath(instance_0_gain,instance_1_gain);
            status= TRUE;
            break;

        default:
            Panic();
            break;
    }

    return status;

}

/******************************************************************************/
bool ancConfigureParallelGainForFBpath(uint8 instance_0_gain, uint8 instance_1_gain)
{
    bool status=FALSE;

    switch(ancDataGetMicParams()->enabled_mics)
    {
        case feed_forward_mode_left_only:
        case feed_back_mode_left_only:
            ancSetParallelGainForFBpathUsingInstanceMask(instance_0_gain,instance_1_gain);
            status= TRUE;
            break;

        case hybrid_mode_left_only:
            ancSetParallelGainForFBpath(instance_0_gain,instance_1_gain);
            status= TRUE;
            break;

        default:
            Panic();
            break;
    }

    return status;
}

/******************************************************************************/
void ancOverWriteWithUserPathGains(void)
{
    anc_user_config_t* config = ancDataGetUserGainConfig();

    if(ancDataIsLeftChannelConfigurable())
    {
       Source mic_source_left = getAnyAncMicSourceForInstance(AUDIO_ANC_INSTANCE_0);

       if(mic_source_left)
       {
           setUserPathGains(mic_source_left, config->left);
       }
    }

    if(ancDataIsRightChannelConfigurable())
    {
       Source mic_source_right = getAnyAncMicSourceForInstance(AUDIO_ANC_INSTANCE_1);

       if(mic_source_right)
       {
           setUserPathGains(mic_source_right, config->right);
       }
    }
}

void ancConfigureParallelFilterPathGains(void)
{
    switch(ancDataGetMicParams()->enabled_mics)
    {
        case feed_forward_mode_left_only:
            setPathGainsParallelAncFFMode();
            break;

        case feed_back_mode_left_only:
            setPathGainsParallelAncFBMode();
            break;

        case hybrid_mode_left_only:
            setPathGainsParallelAncHybridMode();
            break;

        default:
            /*Parallel topology is supported only for Earbuds type of application*/
            ANC_PANIC();
            break;

    }

}

void ancConfigureParallelFilterCoefficients(void)
{
    switch(ancDataGetMicParams()->enabled_mics)
    {
        case feed_forward_mode_left_only:
            ancConfigureParallelCoeficientsForFFMode();
            break;

        case feed_back_mode_left_only:
            ancConfigureParallelCoeficientsForFBMode();
            break;

        case hybrid_mode_left_only:
            ancConfigureParallelCoeficientsForHybridMode();
            break;

        default:
            /*Parallel topology is supported only for Earbuds type of application*/
            ANC_PANIC();
            break;

    }

}

void ancConfigureParallelFilterMutePathGains(void)
{
    switch(ancDataGetMicParams()->enabled_mics)
    {
        case hybrid_mode_left_only:
            ancConfigureParallelGainForFFBpath(ANC_MUTE_GAIN, ANC_MUTE_GAIN);

        case feed_back_mode_left_only: /* fallthrough */
            ancConfigureParallelGainForFBpath(ANC_MUTE_GAIN, ANC_MUTE_GAIN);

        case feed_forward_mode_left_only: /* fallthrough */
            ancConfigureParallelGainForFFApath(ANC_MUTE_GAIN, ANC_MUTE_GAIN);
            break;

        default:
            /*Parallel topology is supported only for Earbuds type of application*/
            ANC_PANIC();
            break;

    }
}

/******************************************************************************/
void ancEnableOutMix(void)
{

    uint32 out_mix_mask;
    Source mic_source =getAnyAncMicSourceForInstance(AUDIO_ANC_INSTANCE_0);

    out_mix_mask = (ACCMD_ANC_CONTROL_OUTMIX_EN_MASK << ACCMD_ANC_CONTROL_ACCESS_SELECT_ENABLES_SHIFT);
    out_mix_mask |= ACCMD_ANC_CONTROL_OUTMIX_EN_MASK;
    PanicFalse(SourceConfigure(mic_source, STREAM_ANC_CONTROL, out_mix_mask));
}

