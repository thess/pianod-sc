///
/// Football message management functions.
/// @file       fb_message.c - Football socket abstraction layer
/// @author     Perette Barella
/// @date       2014-04-08
/// @copyright  Copyright 2012–2014 Devious Fish. All rights reserved.
///

#include <config.h>

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>

#include "fb_public.h"
#include "fb_service.h"


static FB_MESSAGE *freemessages = NULL;
static FB_MESSAGELIST *freeq = NULL;


/* ------------------ Messages -------------------- */
/** @internal
    Free the free message lists */
void fb_free_freelists () {
	FB_MESSAGELIST *free_q;
	FB_MESSAGE *free_messages;
	while ((free_q = freeq)) {
		freeq = freeq->next;
		free (free_q);
	}
	while ((free_messages = freemessages)) {
		freemessages = (FB_MESSAGE *) freemessages->message;
		free (free_messages);
	}
}

/** @internal
    Allocate a message block structure.
    Get it off the free list if its available, otherwise malloc a new one.
    @return the message block, or NULL on failure. */
FB_MESSAGE *fb_messagealloc (void) {
	FB_MESSAGE *result = freemessages;
	if (result) {
		freemessages = (FB_MESSAGE *) (result->message);
	} else {
		result = malloc (sizeof (*result));
	}
	if (result) {
		memset (result, 0, sizeof (*result));
		result->usecount = 1;
	} else {
        fb_perror ("malloc");
	}
	return result;
}

/** @internal
    "Free" a message block by putting it on the free list. */
void fb_messagefree (FB_MESSAGE *freethis) {
	assert (freethis->usecount > 0);
	if (--freethis->usecount <= 0) {
		if (freethis->message) {
			free (freethis->message);
		}
		freethis->message = (char *) freemessages;
		freemessages = freethis;
	}
}

/* ------------------ q / messagelist -------------------- */

/* @internal 
   Allocate a message q structure.
   Allocate it off the free list if available, otherwise malloc a new one.
   @return the message q, or NULL on failure.
 */
FB_MESSAGELIST *fb_qalloc(void) {
	FB_MESSAGELIST *result = freeq;
	if (result) {
		freeq = result->next;
	} else {
		result = malloc (sizeof (*result));
	}
	if (result) {
		memset (result, 0, sizeof (*result));
	} else {
        fb_perror ("malloc");
	}
	return result;
}

/** @internal
    "Free" a single message by putting it on the free list. */
void fb_qfree(FB_MESSAGELIST *freethis) {
	if (freethis->message) {
		fb_messagefree (freethis->message);
	}
	freethis->next = freeq;
	freeq = freethis;
}


/* ------------------ Queues -------------------- */


/** @internal
    Check if a queue empty is empty.
    @return true if the queue is empty. */
bool fb_queue_empty (FB_IOQUEUE *q) {
    assert (q);
    return (q->first == NULL);
}

/** @internal
    Add a message to a queue.
    Does not adjust message block use counts.
    @param queue the queue to add to.
    @param message the message to add to the queue.
    @return true on success, false on failure. */
bool fb_queue_add (FB_IOQUEUE *queue, FB_MESSAGE *message) {
	FB_MESSAGELIST *q = fb_qalloc();
	if (!q) return false;

    q->message = message;
    /* Insert the message at the end of the queue */
    if (queue->first) {
        assert (queue->last);
        queue->last->next = q;
        queue->last = q;
    } else {
        /* Create the queue and enable writes */
        assert (!queue->last);
        queue->first = q;
        queue->last = q;
    }
    return true;
}

/** @internal
    Consume bytes at the front of the queue.
    Bytes consumed must not exceed the remaining bytes in the front message block.
    @param q the queue
    @param consume The number of bytes to consume. */
void fb_queue_consume (FB_IOQUEUE *q, size_t consume) {
    assert (q);
    assert (consume >= 0);
    assert (consume == 0 || q->first);
    /* Skip past the portion transmitted */
    q->consumed += consume;
    assert (q->consumed <= q->first->message->length);
    if (q->consumed >= q->first->message->length) {
        /* We finished with this message. Free it and move to the next. */
        q->consumed = 0;
        FB_MESSAGELIST *freethis = q->first;
        q->first = q->first->next;
        if (q->first == NULL) {
            q->last = NULL;
        }
        fb_qfree (freethis);
    }
}

/** @internal
    Trash everything in a queue.
    @param q the queue to free. */
void fb_queue_destroy (FB_IOQUEUE *q) {
    /* Free all message blocks attached to the output q. */
    FB_MESSAGELIST *m;
    for (m = q->first; m != NULL; m = m->next) {
        fb_messagefree (m->message);
    }
    /* Insert the whole list on the front of the free list */
    if (q->first) {
        q->last->next = freeq;
        freeq = q->first;
    }
    q->first = NULL;
    q->last = NULL;
}
