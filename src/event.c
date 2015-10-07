/*
 *  event.c
 *  pianod - 
 *
 *  Created by Perette Barella on 2013-07-27.
 *  Copyright 2013-2014 Devious Fish. All rights reserved.
 *
 */


#include <stdbool.h>
#include <assert.h>

#include <fb_public.h>
#include <piano.h>

#include "event.h"
#include "response.h"
#include "users.h"



/* Stop reading commands from a connection until after some event occurs */
void wait_for_event (FB_EVENT *event,
					 WAIT_EVENT wait_for) {
	assert (event);
	assert (wait_for > EVENT_NONE);
	assert (event->context);
	
	USER_CONTEXT *context = event->context;
	assert (context->waiting_for == EVENT_NONE);
	context->waiting_for = wait_for;
	fb_accept_input (event->connection, false);
}


/* When an event occurs, find connections waiting for that event and start
   accepting commands from them again. */
void event_occurred (FB_SERVICE *service, WAIT_EVENT whats_happening, int response) {
	assert (service);
	assert (whats_happening > EVENT_NONE);
	
	FB_ITERATOR *it = fb_new_iterator (service);
	if (it) {
		FB_EVENT *event;
		while ((event = fb_iterate_next (it))) {
			USER_CONTEXT *context = event->context;
			if (context->waiting_for == whats_happening && event->type == FB_EVENT_ITERATOR) {
				context->waiting_for = EVENT_NONE;
				fb_accept_input (event->connection, true);
				reply (event, response);
			}
		}
		fb_destroy_iterator (it);
	}
}

