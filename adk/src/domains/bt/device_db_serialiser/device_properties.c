/*!
\copyright  Copyright (c) 2019-2020 Qualcomm Technologies International, Ltd.\n
            All Rights Reserved.\n
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\file
\brief	    Contains helper functions for accessing the device properties data.
*/

#include "device_properties.h"

#include <bdaddr.h>
#include <csrtypes.h>
#include <device.h>
#include <panic.h>
#include <audio_sources_list.h>
#include <voice_sources_list.h>

PRESERVE_TYPE_FOR_DEBUGGING(earbud_device_property_t)

bdaddr DeviceProperties_GetBdAddr(device_t device)
{
    void * object = NULL;
    size_t size = 0;
    PanicFalse(Device_GetProperty(device, device_property_bdaddr, &object, &size));
    PanicFalse(size==sizeof(bdaddr));
    return *((bdaddr *)object);
}

void DeviceProperties_SanitiseBdAddr(bdaddr *bd_addr)
{
    // Sanitise bluetooth address VMCSA-1007
    bdaddr sanitised_bdaddr = {0};
    sanitised_bdaddr.uap = bd_addr->uap;
    sanitised_bdaddr.lap = bd_addr->lap;
    sanitised_bdaddr.nap = bd_addr->nap;
    memcpy(bd_addr, &sanitised_bdaddr, sizeof(bdaddr));
}

void DeviceProperties_SetBdAddr(device_t device, bdaddr *bd_addr)
{
    DeviceProperties_SanitiseBdAddr((bdaddr *)bd_addr);
    Device_SetProperty(device, device_property_bdaddr, (void*)bd_addr, sizeof(bdaddr));
}

audio_source_t DeviceProperties_GetAudioSource(device_t device)
{
    audio_source_t source = audio_source_none;
    if (device)
    {
        audio_source_t *sourcep = NULL;
        size_t size;
        if (Device_GetProperty(device, device_property_audio_source, (void *)&sourcep, &size))
        {
            if (sizeof(audio_source_t) == size)
            {
                source = *sourcep;
            }
        }
    }
    return source;
}

voice_source_t DeviceProperties_GetVoiceSource(device_t device)
{
    voice_source_t source = voice_source_none;
    if (device)
    {
        voice_source_t *sourcep = NULL;
        size_t size;
        if (Device_GetProperty(device, device_property_voice_source, (void *)&sourcep, &size))
        {
            if (sizeof(voice_source_t) == size)
            {
                source = *sourcep;
            }
        }
    }
    return source;
}

void DeviceProperties_SetAudioSource(device_t device, audio_source_t source)
{
    Device_SetProperty(device, device_property_audio_source, &source, sizeof(audio_source_t));
}

void DeviceProperties_SetVoiceSource(device_t device, voice_source_t source)
{
    Device_SetProperty(device, device_property_voice_source, &source, sizeof(audio_source_t));
}

void DeviceProperties_RemoveAudioSource(device_t device)
{
    Device_RemoveProperty(device, device_property_audio_source);
}

void DeviceProperties_RemoveVoiceSource(device_t device)
{
    Device_RemoveProperty(device, device_property_voice_source);
}
