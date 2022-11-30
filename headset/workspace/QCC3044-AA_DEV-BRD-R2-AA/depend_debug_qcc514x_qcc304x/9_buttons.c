#include "input_event_manager.h"
#include "9_buttons.h"

const InputEventConfig_t input_event_config = 
{
	/* Table to convert from PIO to input event ID*/
	{
		 0, -1,  1,  2, -1, -1, -1, -1, -1,  3,  4,  5,  6,  7,  8, -1, 
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
	},

	/* Masks for each PIO bank to configure as inputs */
	{ 0x00007e0dUL, 0x00000000UL, 0x00000000UL },
	/* PIO debounce settings */
	4, 5
};


const InputActionMessage_t default_message_group[37] = 
{
	{
		MUSIC,                                  /* Input event bits */
		MUSIC,                                  /* Input event mask */
		ENTER,                                  /* Action */
		0,                                      /* Timeout */
		0,                                      /* Repeat */
		APP_BUTTON_PLAY_PAUSE_TOGGLE,           /* Message */
	},
	{
		VOL_PLUS,                               /* Input event bits */
		VOL_PLUS | VOL_MINUS,                   /* Input event mask */
		ENTER,                                  /* Action */
		0,                                      /* Timeout */
		0,                                      /* Repeat */
		APP_BUTTON_VOLUME_UP,                   /* Message */
	},
	{
		SW8,                                    /* Input event bits */
		SW8,                                    /* Input event mask */
		ENTER,                                  /* Action */
		0,                                      /* Timeout */
		0,                                      /* Repeat */
		APP_VA_BUTTON_DOWN,                     /* Message */
	},
	{
		VOL_MINUS,                              /* Input event bits */
		VOL_MINUS | VOL_PLUS,                   /* Input event mask */
		ENTER,                                  /* Action */
		0,                                      /* Timeout */
		0,                                      /* Repeat */
		APP_BUTTON_VOLUME_DOWN,                 /* Message */
	},
	{
		SYS_CTRL,                               /* Input event bits */
		SYS_CTRL,                               /* Input event mask */
		HELD,                                   /* Action */
		6000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_MFB_BUTTON_HELD_6SEC,               /* Message */
	},
	{
		SW8,                                    /* Input event bits */
		SW8,                                    /* Input event mask */
		HELD,                                   /* Action */
		1000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_VA_BUTTON_HELD_1SEC,                /* Message */
	},
	{
		SYS_CTRL,                               /* Input event bits */
		SYS_CTRL,                               /* Input event mask */
		HELD,                                   /* Action */
		1000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_MFB_BUTTON_HELD_1SEC,               /* Message */
	},
	{
		SYS_CTRL,                               /* Input event bits */
		SYS_CTRL,                               /* Input event mask */
		HELD,                                   /* Action */
		8000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_MFB_BUTTON_HELD_8SEC,               /* Message */
	},
	{
		SYS_CTRL,                               /* Input event bits */
		SYS_CTRL,                               /* Input event mask */
		HELD,                                   /* Action */
		15000,                                  /* Timeout */
		0,                                      /* Repeat */
		APP_BUTTON_HELD_FACTORY_RESET,          /* Message */
	},
	{
		BACK,                                   /* Input event bits */
		BACK,                                   /* Input event mask */
		HELD,                                   /* Action */
		500,                                    /* Timeout */
		0,                                      /* Repeat */
		APP_BUTTON_BACKWARD_HELD,               /* Message */
	},
	{
		MFB,                                    /* Input event bits */
		MFB,                                    /* Input event mask */
		HELD,                                   /* Action */
		6000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_ANC_TOGGLE_ON_OFF,                  /* Message */
	},
	{
		FORWARD,                                /* Input event bits */
		FORWARD,                                /* Input event mask */
		HELD,                                   /* Action */
		500,                                    /* Timeout */
		0,                                      /* Repeat */
		APP_BUTTON_FORWARD_HELD,                /* Message */
	},
	{
		SYS_CTRL,                               /* Input event bits */
		SYS_CTRL,                               /* Input event mask */
		HELD,                                   /* Action */
		3000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_MFB_BUTTON_HELD_3SEC,               /* Message */
	},
	{
		SYS_CTRL,                               /* Input event bits */
		SYS_CTRL,                               /* Input event mask */
		HELD,                                   /* Action */
		12000,                                  /* Timeout */
		0,                                      /* Repeat */
		APP_BUTTON_HELD_DFU,                    /* Message */
	},
	{
		BACK,                                   /* Input event bits */
		BACK,                                   /* Input event mask */
		SINGLE_CLICK,                           /* Action */
		0,                                      /* Timeout */
		0,                                      /* Repeat */
		APP_BUTTON_BACKWARD,                    /* Message */
	},
	{
		VOICE,                                  /* Input event bits */
		VOICE,                                  /* Input event mask */
		SINGLE_CLICK,                           /* Action */
		0,                                      /* Timeout */
		0,                                      /* Repeat */
		APP_ANC_ENABLE,                         /* Message */
	},
	{
		SYS_CTRL,                               /* Input event bits */
		SYS_CTRL,                               /* Input event mask */
		SINGLE_CLICK,                           /* Action */
		0,                                      /* Timeout */
		0,                                      /* Repeat */
		APP_MFB_BUTTON_SINGLE_CLICK,            /* Message */
	},
	{
		SW8,                                    /* Input event bits */
		SW8,                                    /* Input event mask */
		SINGLE_CLICK,                           /* Action */
		0,                                      /* Timeout */
		0,                                      /* Repeat */
		APP_VA_BUTTON_SINGLE_CLICK,             /* Message */
	},
	{
		FORWARD,                                /* Input event bits */
		FORWARD,                                /* Input event mask */
		SINGLE_CLICK,                           /* Action */
		0,                                      /* Timeout */
		0,                                      /* Repeat */
		APP_BUTTON_FORWARD,                     /* Message */
	},
	{
		SYS_CTRL,                               /* Input event bits */
		SYS_CTRL,                               /* Input event mask */
		HELD_RELEASE,                           /* Action */
		6000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_MFB_BUTTON_HELD_RELEASE_6SEC,       /* Message */
	},
	{
		SYS_CTRL,                               /* Input event bits */
		SYS_CTRL,                               /* Input event mask */
		HELD_RELEASE,                           /* Action */
		8000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_MFB_BUTTON_HELD_RELEASE_8SEC,       /* Message */
	},
	{
		SYS_CTRL,                               /* Input event bits */
		SYS_CTRL,                               /* Input event mask */
		HELD_RELEASE,                           /* Action */
		1000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_MFB_BUTTON_1_SECOND,                /* Message */
	},
	{
		SYS_CTRL,                               /* Input event bits */
		SYS_CTRL,                               /* Input event mask */
		HELD_RELEASE,                           /* Action */
		15000,                                  /* Timeout */
		0,                                      /* Repeat */
		APP_BUTTON_FACTORY_RESET,               /* Message */
	},
	{
		FORWARD,                                /* Input event bits */
		FORWARD,                                /* Input event mask */
		HELD_RELEASE,                           /* Action */
		500,                                    /* Timeout */
		0,                                      /* Repeat */
		APP_BUTTON_FORWARD_HELD_RELEASE,        /* Message */
	},
	{
		MFB,                                    /* Input event bits */
		MFB,                                    /* Input event mask */
		HELD_RELEASE,                           /* Action */
		4000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_ANC_SET_NEXT_MODE,                  /* Message */
	},
	{
		VOICE,                                  /* Input event bits */
		VOICE,                                  /* Input event mask */
		HELD_RELEASE,                           /* Action */
		3000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_ANC_DISABLE,                        /* Message */
	},
	{
		SYS_CTRL,                               /* Input event bits */
		SYS_CTRL,                               /* Input event mask */
		HELD_RELEASE,                           /* Action */
		12000,                                  /* Timeout */
		0,                                      /* Repeat */
		APP_BUTTON_DFU,                         /* Message */
	},
	{
		SW8,                                    /* Input event bits */
		SW8,                                    /* Input event mask */
		HELD_RELEASE,                           /* Action */
		1000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_VA_BUTTON_HELD_RELEASE,             /* Message */
	},
	{
		SW8,                                    /* Input event bits */
		SW8,                                    /* Input event mask */
		HELD_RELEASE,                           /* Action */
		6000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_LEAKTHROUGH_SET_NEXT_MODE,          /* Message */
	},
	{
		SYS_CTRL,                               /* Input event bits */
		SYS_CTRL,                               /* Input event mask */
		HELD_RELEASE,                           /* Action */
		3000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_MFB_BUTTON_HELD_RELEASE_3SEC,       /* Message */
	},
	{
		SYS_CTRL,                               /* Input event bits */
		SYS_CTRL,                               /* Input event mask */
		HELD_RELEASE,                           /* Action */
		1000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_MFB_BUTTON_HELD_RELEASE_1SEC,       /* Message */
	},
	{
		BACK,                                   /* Input event bits */
		BACK,                                   /* Input event mask */
		HELD_RELEASE,                           /* Action */
		500,                                    /* Timeout */
		0,                                      /* Repeat */
		APP_BUTTON_BACKWARD_HELD_RELEASE,       /* Message */
	},
	{
		SYS_CTRL,                               /* Input event bits */
		SYS_CTRL,                               /* Input event mask */
		HELD_RELEASE,                           /* Action */
		2000,                                   /* Timeout */
		0,                                      /* Repeat */
		APP_LEAKTHROUGH_TOGGLE_ON_OFF,          /* Message */
	},
	{
		SW8,                                    /* Input event bits */
		SW8,                                    /* Input event mask */
		DOUBLE_CLICK,                           /* Action */
		0,                                      /* Timeout */
		0,                                      /* Repeat */
		APP_VA_BUTTON_DOUBLE_CLICK,             /* Message */
	},
	{
		SW8,                                    /* Input event bits */
		SW8,                                    /* Input event mask */
		RELEASE,                                /* Action */
		0,                                      /* Timeout */
		0,                                      /* Repeat */
		APP_VA_BUTTON_RELEASE,                  /* Message */
	},
	{
		VOL_PLUS,                               /* Input event bits */
		VOL_PLUS,                               /* Input event mask */
		RELEASE,                                /* Action */
		0,                                      /* Timeout */
		0,                                      /* Repeat */
		APP_BUTTON_VOLUME_UP_RELEASE,           /* Message */
	},
	{
		VOL_MINUS,                              /* Input event bits */
		VOL_MINUS,                              /* Input event mask */
		RELEASE,                                /* Action */
		0,                                      /* Timeout */
		0,                                      /* Repeat */
		APP_BUTTON_VOLUME_DOWN_RELEASE,         /* Message */
	},
};


ASSERT_MESSAGE_GROUP_NOT_OVERFLOWED(LOGICAL_INPUT,MAX_INPUT_ACTION_MESSAGE_ID)

