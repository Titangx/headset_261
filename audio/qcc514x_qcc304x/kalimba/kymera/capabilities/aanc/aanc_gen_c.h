// -----------------------------------------------------------------------------
// Copyright (c) 2021                  Qualcomm Technologies International, Ltd.
//
#ifndef __AANC_GEN_C_H__
#define __AANC_GEN_C_H__

#ifndef ParamType
typedef unsigned ParamType;
#endif

// CodeBase IDs
#define AANC_AANC_MONO_16K_CAP_ID	0x00C7
#define AANC_AANC_MONO_16K_ALT_CAP_ID_0	0x409F
#define AANC_AANC_MONO_16K_SAMPLE_RATE	16000
#define AANC_AANC_MONO_16K_VERSION_MAJOR	1

// Constant Definitions
#define AANC_CONSTANT_IN_OUT_EAR_CTRL    		0x00000003
#define AANC_CONSTANT_FF_FINE_GAIN_CTRL  		0x00000004
#define AANC_CONSTANT_CHANNEL_CTRL       		0x00000005
#define AANC_CONSTANT_FEEDFORWARD_CTRL   		0x00000006
#define AANC_CONSTANT_FF_COARSE_GAIN_CTRL		0x00000007
#define AANC_CONSTANT_FB_FINE_GAIN_CTRL  		0x00000008
#define AANC_CONSTANT_FB_COARSE_GAIN_CTRL		0x00000009
#define AANC_CONSTANT_EC_FINE_GAIN_CTRL  		0x0000000A
#define AANC_CONSTANT_EC_COARSE_GAIN_CTRL		0x0000000B
#define AANC_CONSTANT_FILTER_CONFIG_CTRL 		0x0000000C


// Runtime Config Parameter Bitfields
// AANC_CONFIG bits
#define AANC_CONFIG_AANC_CONFIG_DISABLE_ED_INT              		0x00000001
#define AANC_CONFIG_AANC_CONFIG_DISABLE_ED_EXT              		0x00000002
#define AANC_CONFIG_AANC_CONFIG_DISABLE_ED_PB               		0x00000004
#define AANC_CONFIG_AANC_CONFIG_DISABLE_SELF_SPEECH         		0x00000008
// DISABLE_AG_CALC bits
#define AANC_CONFIG_DISABLE_AG_CALC_DISABLE_AG_CALC         		0x00000001
// AANC_DEBUG bits
#define AANC_CONFIG_AANC_DEBUG_DISABLE_AG_MODEL_CHECK       		0x00000001
#define AANC_CONFIG_AANC_DEBUG_DISABLE_EAR_STATUS_CHECK     		0x00000002
#define AANC_CONFIG_AANC_DEBUG_DISABLE_ANC_CLOCK_CHECK      		0x00000004
#define AANC_CONFIG_AANC_DEBUG_DISABLE_EVENT_MESSAGING      		0x00000008
#define AANC_CONFIG_AANC_DEBUG_DISABLE_ED_INT_E_FILTER_CHECK		0x00000010
#define AANC_CONFIG_AANC_DEBUG_DISABLE_ED_EXT_E_FILTER_CHECK		0x00000020
#define AANC_CONFIG_AANC_DEBUG_DISABLE_ED_PB_E_FILTER_CHECK 		0x00000040
#define AANC_CONFIG_AANC_DEBUG_MUX_SEL_ALGORITHM            		0x00000100
#define AANC_CONFIG_AANC_DEBUG_DISABLE_FILTER_OPTIM         		0x00000200
#define AANC_CONFIG_AANC_DEBUG_DISABLE_CLIPPING_DETECT_INT  		0x00001000
#define AANC_CONFIG_AANC_DEBUG_DISABLE_CLIPPING_DETECT_EXT  		0x00002000
#define AANC_CONFIG_AANC_DEBUG_DISABLE_CLIPPING_DETECT_PB   		0x00004000


// System Mode
#define AANC_SYSMODE_STANDBY    		0
#define AANC_SYSMODE_MUTE_ANC   		1
#define AANC_SYSMODE_FULL       		2
#define AANC_SYSMODE_STATIC     		3
#define AANC_SYSMODE_FREEZE     		4
#define AANC_SYSMODE_GENTLE_MUTE		5
#define AANC_SYSMODE_QUIET      		6
#define AANC_SYSMODE_MAX_MODES  		7

// System Control
#define AANC_CONTROL_MODE_OVERRIDE		0x2000

// In/Out of Ear Status
#define AANC_IN_OUT_EAR_CTRL_IN_EAR 		0x0001
#define AANC_IN_OUT_EAR_CTRL_OUT_EAR		0x0000

// Selected Channel
#define AANC_CHANNEL_ANC0		0x0001
#define AANC_CHANNEL_ANC1		0x0002

// Selected Filter Configuration
#define AANC_FILTER_CONFIG_SINGLE  		0x0000
#define AANC_FILTER_CONFIG_PARALLEL		0x0001

// Selected Path
#define AANC_FEEDFORWARD_PATH_FFA		0x0001
#define AANC_FEEDFORWARD_PATH_FFB		0x0002

// License Status
#define AANC_LICENSE_STATUS_ED                    		0x00000001
#define AANC_LICENSE_STATUS_FxLMS                 		0x00000002
#define AANC_LICENSE_STATUS_LICENSING_BUILD_STATUS		0x10000000

// Flags
#define AANC_FLAGS_ED_INT            		0x00000010
#define AANC_FLAGS_ED_EXT            		0x00000020
#define AANC_FLAGS_ED_PLAYBACK       		0x00000040
#define AANC_FLAGS_SELF_SPEECH       		0x00000080
#define AANC_FLAGS_CLIPPING_INT      		0x00000100
#define AANC_FLAGS_CLIPPING_EXT      		0x00000200
#define AANC_FLAGS_CLIPPING_PLAYBACK 		0x00000400
#define AANC_FLAGS_SATURATION_INT    		0x00001000
#define AANC_FLAGS_SATURATION_EXT    		0x00002000
#define AANC_FLAGS_SATURATION_PLANT  		0x00004000
#define AANC_FLAGS_SATURATION_CONTROL		0x00008000
#define AANC_FLAGS_QUIET_MODE        		0x00100000

// ANC FF Fine Gain

// ANC FF Coarse Gain

// ANC FB Fine Gain

// ANC FB Coarse Gain

// ANC EC Fine Gain

// ANC EC Coarse Gain

// Statistics Block
typedef struct _tag_AANC_STATISTICS
{
	ParamType OFFSET_CUR_MODE;
	ParamType OFFSET_OVR_CONTROL;
	ParamType OFFSET_IN_OUT_EAR_CTRL;
	ParamType OFFSET_CHANNEL;
	ParamType OFFSET_FILTER_CONFIG;
	ParamType OFFSET_FEEDFORWARD_PATH;
	ParamType OFFSET_LICENSE_STATUS;
	ParamType OFFSET_FLAGS;
	ParamType OFFSET_AG_CALC;
	ParamType OFFSET_FF_FINE_GAIN_CTRL;
	ParamType OFFSET_FF_COARSE_GAIN_CTRL;
	ParamType OFFSET_FF_GAIN_DB;
	ParamType OFFSET_FB_FINE_GAIN_CTRL;
	ParamType OFFSET_FB_COARSE_GAIN_CTRL;
	ParamType OFFSET_FB_GAIN_DB;
	ParamType OFFSET_EC_FINE_GAIN_CTRL;
	ParamType OFFSET_EC_COARSE_GAIN_CTRL;
	ParamType OFFSET_EC_GAIN_DB;
	ParamType OFFSET_SPL_EXT;
	ParamType OFFSET_SPL_INT;
	ParamType OFFSET_SPL_PB;
	ParamType OFFSET_PEAK_EXT;
	ParamType OFFSET_PEAK_INT;
	ParamType OFFSET_PEAK_PB;
}AANC_STATISTICS;

typedef AANC_STATISTICS* LP_AANC_STATISTICS;

// Parameters Block
typedef struct _tag_AANC_PARAMETERS
{
	ParamType OFFSET_AANC_CONFIG;
	ParamType OFFSET_DISABLE_AG_CALC;
	ParamType OFFSET_MU;
	ParamType OFFSET_GAMMA;
	ParamType OFFSET_GENTLE_MUTE_TIMER;
	ParamType OFFSET_AANC_DEBUG;
	ParamType OFFSET_QUIET_MODE_HI_THRESHOLD;
	ParamType OFFSET_QUIET_MODE_LO_THRESHOLD;
	ParamType OFFSET_ED_EXT_ATTACK;
	ParamType OFFSET_ED_EXT_DECAY;
	ParamType OFFSET_ED_EXT_ENVELOPE;
	ParamType OFFSET_ED_EXT_INIT_FRAME;
	ParamType OFFSET_ED_EXT_RATIO;
	ParamType OFFSET_ED_EXT_MIN_SIGNAL;
	ParamType OFFSET_ED_EXT_MIN_MAX_ENVELOPE;
	ParamType OFFSET_ED_EXT_DELTA_TH;
	ParamType OFFSET_ED_EXT_COUNT_TH;
	ParamType OFFSET_ED_EXT_HOLD_FRAMES;
	ParamType OFFSET_ED_EXT_E_FILTER_MIN_THRESHOLD;
	ParamType OFFSET_ED_EXT_E_FILTER_MIN_COUNTER_THRESHOLD;
	ParamType OFFSET_ED_INT_ATTACK;
	ParamType OFFSET_ED_INT_DECAY;
	ParamType OFFSET_ED_INT_ENVELOPE;
	ParamType OFFSET_ED_INT_INIT_FRAME;
	ParamType OFFSET_ED_INT_RATIO;
	ParamType OFFSET_ED_INT_MIN_SIGNAL;
	ParamType OFFSET_ED_INT_MIN_MAX_ENVELOPE;
	ParamType OFFSET_ED_INT_DELTA_TH;
	ParamType OFFSET_ED_INT_COUNT_TH;
	ParamType OFFSET_ED_INT_HOLD_FRAMES;
	ParamType OFFSET_ED_INT_E_FILTER_MIN_THRESHOLD;
	ParamType OFFSET_ED_INT_E_FILTER_MIN_COUNTER_THRESHOLD;
	ParamType OFFSET_ED_PB_ATTACK;
	ParamType OFFSET_ED_PB_DECAY;
	ParamType OFFSET_ED_PB_ENVELOPE;
	ParamType OFFSET_ED_PB_INIT_FRAME;
	ParamType OFFSET_ED_PB_RATIO;
	ParamType OFFSET_ED_PB_MIN_SIGNAL;
	ParamType OFFSET_ED_PB_MIN_MAX_ENVELOPE;
	ParamType OFFSET_ED_PB_DELTA_TH;
	ParamType OFFSET_ED_PB_COUNT_TH;
	ParamType OFFSET_ED_PB_HOLD_FRAMES;
	ParamType OFFSET_ED_PB_E_FILTER_MIN_THRESHOLD;
	ParamType OFFSET_ED_PB_E_FILTER_MIN_COUNTER_THRESHOLD;
	ParamType OFFSET_BPF_DENOMINATOR_COEFF_EXT_0;
	ParamType OFFSET_BPF_DENOMINATOR_COEFF_EXT_1;
	ParamType OFFSET_BPF_DENOMINATOR_COEFF_EXT_2;
	ParamType OFFSET_BPF_DENOMINATOR_COEFF_EXT_3;
	ParamType OFFSET_BPF_DENOMINATOR_COEFF_EXT_4;
	ParamType OFFSET_BPF_NUMERATOR_COEFF_EXT_0;
	ParamType OFFSET_BPF_NUMERATOR_COEFF_EXT_1;
	ParamType OFFSET_BPF_NUMERATOR_COEFF_EXT_2;
	ParamType OFFSET_BPF_NUMERATOR_COEFF_EXT_3;
	ParamType OFFSET_BPF_NUMERATOR_COEFF_EXT_4;
	ParamType OFFSET_BPF_DENOMINATOR_COEFF_INT_0;
	ParamType OFFSET_BPF_DENOMINATOR_COEFF_INT_1;
	ParamType OFFSET_BPF_DENOMINATOR_COEFF_INT_2;
	ParamType OFFSET_BPF_DENOMINATOR_COEFF_INT_3;
	ParamType OFFSET_BPF_DENOMINATOR_COEFF_INT_4;
	ParamType OFFSET_BPF_NUMERATOR_COEFF_INT_0;
	ParamType OFFSET_BPF_NUMERATOR_COEFF_INT_1;
	ParamType OFFSET_BPF_NUMERATOR_COEFF_INT_2;
	ParamType OFFSET_BPF_NUMERATOR_COEFF_INT_3;
	ParamType OFFSET_BPF_NUMERATOR_COEFF_INT_4;
	ParamType OFFSET_CLIPPING_DURATION_EXT;
	ParamType OFFSET_CLIPPING_DURATION_INT;
	ParamType OFFSET_CLIPPING_DURATION_PB;
	ParamType OFFSET_FXLMS_MIN_BOUND;
	ParamType OFFSET_FXLMS_MAX_BOUND;
	ParamType OFFSET_FXLMS_MAX_DELTA;
	ParamType OFFSET_FXLMS_INITIAL_VALUE;
	ParamType OFFSET_EVENT_GAIN_STUCK;
	ParamType OFFSET_EVENT_ED_STUCK;
	ParamType OFFSET_EVENT_QUIET_DETECT;
	ParamType OFFSET_EVENT_QUIET_CLEAR;
	ParamType OFFSET_EVENT_CLIP_STUCK;
	ParamType OFFSET_EVENT_SAT_STUCK;
	ParamType OFFSET_EVENT_SELF_TALK;
	ParamType OFFSET_SELF_SPEECH_THRESHOLD;
	ParamType OFFSET_LAMBDA;
	ParamType OFFSET_TARGET_NOISE_REDUCTION;
	ParamType OFFSET_QUIET_MODE_TIMER;
	ParamType OFFSET_EVENT_SPL;
	ParamType OFFSET_EVENT_SPL_THRESHOLD;
}AANC_PARAMETERS;

typedef AANC_PARAMETERS* LP_AANC_PARAMETERS;

unsigned *AANC_GetDefaults(unsigned capid);

#endif // __AANC_GEN_C_H__
