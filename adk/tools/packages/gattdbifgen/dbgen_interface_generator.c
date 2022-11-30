/*!
\copyright  Copyright (c) 2020 Qualcomm Technologies International, Ltd.\n
            All Rights Reserved.\n
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version    
\file       ${module}_db_if.c
\defgroup   ${module}_db
\brief      This implements an interface to allow common code to access application
            specific GATT database service constants.
*/

/*
 * THIS FILE IS AUTOGENERATED, DO NOT EDIT!
 *
 * generated by dbgen_interface_generator.py
 */

#include <csrtypes.h>
#include <logging.h>
#include <panic.h>

#include "${module}_db.h"
#include "${module}_db_if.h"

uint16 getGattAttributeValue(gatt_attribute_t id, uint16 n)
{
    switch ( id )
    {
${switch_cases}
    }
    /* Check the GATT related conditional flags used to compile the ADK and the application. */
    /* The ADK just asked for an attribute value that the Application wasn't compiled for. */
    DEBUG_LOG("getGattAttributeValue lookup for id=%d(%x), n=%d failed", id, id, n);
    Panic();
    /* NOTREACHED */
    return 0;
}
