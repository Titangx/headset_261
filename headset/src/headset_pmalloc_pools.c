/*!
\copyright  Copyright (c) 2019 - 2020 Qualcomm Technologies International, Ltd.\n
            All Rights Reserved.\n
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\file       headset_pmalloc_pools.c
\brief      Definition of the pmalloc pools used by the headset application.

The pools defined here will be merged at run time with the base definitions
from Hydra OS - see 'pmalloc_config_P1.h'.
*/

#include <pmalloc.h>

_Pragma ("unitsuppress Unused")

_Pragma ("datasection apppool")

static const pmalloc_pool_config app_pools[] =
{
    {   4, 15 },
    {   8, 25 },
    {  12, 32 },
    {  16, 13 },
    {  20, 31 },
    {  24, 20 },
    {  28, 55 },
    {  32, 21 },
    {  36, 19 },
    {  40, 10 },
    {  56,  9 },
    {  64,  7 },
    {  80,  9 },
    { 120, 14 },
    { 160,  8 },
    { 180,  2 },
    { 220,  2 },
    { 288,  1 }, /* for theGatt = gattState */
    { 512,  1 },
    { 692,  3 }

    /* Including the pools in pmalloc_config_P1.h:
       Total pools:  20
       Total blocks: 325
       Total bytes:  14076 + (20 * 24) = 14556
    */
};

