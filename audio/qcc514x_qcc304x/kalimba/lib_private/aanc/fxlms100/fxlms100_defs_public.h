/****************************************************************************
 * Copyright (c) 2015 - 2020 Qualcomm Technologies International, Ltd.
****************************************************************************/
/**
 * \defgroup lib_private\aanc
 *
 * \file  fxlms100_defs_public.h
 * \ingroup lib_private\aanc
 *
 * FXLMS100 library header file providing public definitions common to C and
 * ASM code.
 */
#ifndef _FXLMS100_LIB_DEFS_PUBLIC_H_
#define _FXLMS100_LIB_DEFS_PUBLIC_H_

/******************************************************************************
Public Constant Definitions
*/

/* Enable saturation detection in the build */
#define DETECT_SATURATION

/* Flag definitions that are set during processing */
#define FXLMS100_FLAGS_SATURATION_INT_SHIFT      12
#define FXLMS100_FLAGS_SATURATION_EXT_SHIFT      13
#define FXLMS100_FLAGS_SATURATION_PLANT_SHIFT    14
#define FXLMS100_FLAGS_SATURATION_CONTROL_SHIFT  15

#define FXLMS100_BANDPASS_SHIFT                   3
#define FXLMS100_PLANT_SHIFT                      2
#define FXLMS100_CONTROL_SHIFT                    2

#define FXLMS100_GAIN_SHIFT                      23

/* Frame size used to allocate temporary buffer. */
#define FXLMS100_FRAME_SIZE                      64

/* Indicate whether to run the FxLMS calculation based on a single filter or
 * parallel filter configuration.
 */
#define FXLMS100_CONFIG_SINGLE                    0x0000
#define FXLMS100_CONFIG_PARALLEL                  0x0001
#define FLXMS100_CONFIG_LAYOUT_MASK               0x000F
#define FXLMS100_CONFIG_LAYOUT_MASK_INV           (FLXMS100_CONFIG_LAYOUT_MASK ^ \
                                                   0xFFFF)

#define FXLMS100_CONFIG_ENABLE_FILTER_OPTIM       0x0000
#define FXLMS100_CONFIG_DISABLE_FILTER_OPTIM      0x0100
#define FXLMS100_CONFIG_OPTIM_MASK                0x0F00
#define FXLMS100_CONIFIG_OPTIM_MASK_INV           (FXLMS100_CONFIG_OPTIM_MASK ^ \
                                                   0xFFFF)

#endif /* _FXLMS100_LIB_DEFS_PUBLIC_H_ */
