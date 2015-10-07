/*
 *  event.h
 *  pianod
 *
 *  Created by Perette Barella on 2013-07-27.
 *  Copyright 2013-2014 Devious Fish. All rights reserved.
 *
 */

#ifndef _EVENT_H
#define _EVENT_H

#include <fb_public.h>

typedef enum wait_event_t {
	EVENT_NONE = 0,
	EVENT_AUTHENTICATED,
	EVENT_TRACK_ENDED,
	EVENT_TRACK_STARTED
} WAIT_EVENT;

extern void wait_for_event (FB_EVENT *event,
							WAIT_EVENT wait_for);
extern void event_occurred (FB_SERVICE *service, WAIT_EVENT event, int response);

#endif /* __EVENT_H__ */
