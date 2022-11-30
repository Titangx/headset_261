/*!
\copyright  Copyright (c) 2020 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version    %%version
\file       headset_prompts_config_table.c
\brief      Headset prompts configuration table module
*/
#include "headset_prompts_config_table.h"

#include <domain_message.h>
#include <ui_prompts.h>
#include <av.h>
#include <pairing.h>
#include <telephony_messages.h>
#include <power_manager.h>
#include <voice_ui.h>

#ifdef INCLUDE_PROMPTS
const ui_event_indicator_table_t headset_ui_prompts_table[] =
{
#ifndef EXCLUDE_POWER_PROMPTS
    {.sys_event=POWER_ON,                {.prompt.filename = "power_on.sbc",
                                          .prompt.rate = 48000,
                                          .prompt.format = PROMPT_FORMAT_SBC,
                                          .prompt.interruptible = FALSE,
                                          .prompt.queueable = TRUE,
                                          .prompt.requires_repeat_delay = TRUE },
                                          .await_indication_completion = TRUE },
    {.sys_event=POWER_OFF,              { .prompt.filename = "power_off.sbc",
                                          .prompt.rate = 48000,
                                          .prompt.format = PROMPT_FORMAT_SBC,
                                          .prompt.interruptible = FALSE,
                                          .prompt.queueable = TRUE,
                                          .prompt.requires_repeat_delay = TRUE },
                                          .await_indication_completion = TRUE },
#endif
    {.sys_event=PAIRING_ACTIVE,         { .prompt.filename = "pairing.sbc",
                                          .prompt.rate = 48000,
                                          .prompt.format = PROMPT_FORMAT_SBC,
                                          .prompt.interruptible = FALSE,
                                          .prompt.queueable = TRUE,
                                          .prompt.requires_repeat_delay = TRUE }},
    {.sys_event=PAIRING_COMPLETE,       { .prompt.filename = "pairing_successful.sbc",
                                          .prompt.rate = 48000,
                                          .prompt.format = PROMPT_FORMAT_SBC,
                                          .prompt.interruptible = FALSE,
                                          .prompt.queueable = TRUE,
                                          .prompt.requires_repeat_delay = TRUE }},
    {.sys_event=PAIRING_FAILED,         { .prompt.filename = "pairing_failed.sbc",
                                          .prompt.rate = 48000,
                                          .prompt.format = PROMPT_FORMAT_SBC,
                                          .prompt.interruptible = FALSE,
                                          .prompt.queueable = TRUE,
                                          .prompt.requires_repeat_delay = TRUE }},
#ifndef EXCLUDE_CONN_PROMPTS
    {.sys_event=TELEPHONY_CONNECTED,    { .prompt.filename = "connected.sbc",
                                          .prompt.rate = 48000,
                                          .prompt.format = PROMPT_FORMAT_SBC,
                                          .prompt.interruptible = FALSE,
                                          .prompt.queueable = TRUE,
                                          .prompt.requires_repeat_delay = FALSE }},
    {.sys_event=TELEPHONY_DISCONNECTED, { .prompt.filename = "disconnected.sbc",
                                          .prompt.rate = 48000,
                                          .prompt.format = PROMPT_FORMAT_SBC,
                                          .prompt.interruptible = FALSE,
                                          .prompt.queueable = TRUE,
                                          .prompt.requires_repeat_delay = FALSE }},
#endif
    {.sys_event=VOICE_UI_MIC_OPEN,      { .prompt.filename = "mic_open.sbc",
                                          .prompt.rate = 16000,
                                          .prompt.format = PROMPT_FORMAT_SBC,
                                          .prompt.interruptible = FALSE,
                                          .prompt.queueable = FALSE,
                                          .prompt.requires_repeat_delay = FALSE }},
    {.sys_event=VOICE_UI_MIC_CLOSE,     { .prompt.filename = "mic_close.sbc",
                                          .prompt.rate = 16000,
                                          .prompt.format = PROMPT_FORMAT_SBC,
                                          .prompt.interruptible = TRUE,
                                          .prompt.queueable = FALSE,
                                          .prompt.requires_repeat_delay = FALSE }},
    {.sys_event=VOICE_UI_DISCONNECTED,  { .prompt.filename = "bt_va_not_connected.sbc",
                                          .prompt.rate = 48000,
                                          .prompt.format = PROMPT_FORMAT_SBC,
                                          .prompt.interruptible = FALSE,
                                          .prompt.queueable = TRUE,
                                          .prompt.requires_repeat_delay = TRUE }}
};
#endif
uint8 HeadsetPromptsConfigTable_GetSize(void)
{
        #ifdef INCLUDE_PROMPTS
            return ARRAY_DIM(headset_ui_prompts_table);
        #else
            return 0;
        #endif
}

