///
/// Football socket management and dispatching.
/// @file       fb_socketmgr.c - Football socket abstraction layer
/// @author     Perette Barella
/// @date       2012-03-03
/// @copyright  Copyright 2012–2014 Devious Fish. All rights reserved.
///

#include <config.h>

#ifndef __FreeBSD__
#define _POSIX_C_SOURCE 1 /* fileno() */
#define _DEFAULT_SOURCE /* strdup() */
#define _DARWIN_C_SOURCE /* strdup() on OS X */
#endif
#ifdef __linux__
#include <linux/posix_types.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/select.h>

#include <assert.h>

#include "fb_service.h"

/* Some Linuxes seem to be missing these */
#ifndef FD_COPY
#define FD_COPY(from,to) memmove(to, from, sizeof(*(from)))
#endif

/* The strategy here is to just register sockets in an array by their socket number.
   When socket N needs attention, it's easy and fast to find.  We also store the
   socket number in with that data, so can refer back. */
typedef struct socket_data_t {
	int socket;
	FB_SOCKETTYPE type;
	union {
		FB_CONNECTION *connection;
		FB_SERVICE *service;
		void *user;
	} thingie; /* Holds one of those thingies, y'know? */
} FB_SOCKET_DATA;

/* We need 3 bit arrays for the select() system call, one for each possible
   I/O state for each socket. We need a fourth for GNUTLS, which may buffer too. */
typedef enum select_action_t {
	ACTION_READING,
	ACTION_WRITING,
	ACTION_FAULTING,
    ACTION_BUFFERING,
	ACTION_COUNT,
    ACTION_SELECT_COUNT = 3
} ACTION;


static FB_SERVICE *reapq = NULL;
static FB_SOCKET_DATA **sockets; /**< Index by socket number to service/connection/etc. */
static int maxsockets = 0;
static int activesockets = 0;
static fd_set select_state [ACTION_COUNT];	/**< What we'll use on the next select() */
static fd_set last_state [ACTION_SELECT_COUNT];	/**< What came back on the last select() */
static FB_EVENT *queued_event = NULL; /**< Pending event (NULL if none, or single event. */
static bool tls_currently_buffering; /**< Set when TLS has stuff in its buffers. */

/** @internal
    Schedule a service for reaping when it has no connections left. */
void fb_schedule_reap (FB_SERVICE *service) {
	/* It's called reapq, but they're stacked. Order doesn't matter. */
	service->next_reap = reapq;
	reapq = service;
}

/** @internal
    Queue an event.
    Only used for fallback mode on HTTP port, when we need to
    deliver a connection arrival and then the command, so size is limited
    to 1.
    @param event the event to queue
*/
void fb_queue_event (FB_EVENT *event) {
    assert (!queued_event);
    queued_event = event;
}

/** @internal
    Destroy an event and free its resources.
    Events are all statically allocated, so we never free them
    in the current implementation.  However, dispose frees the
    dynamically allocated elements and clears out the structure.
    @param event the event to free */
void fb_dispose_event (FB_EVENT *event) {
	free (event->command);
	fb_destroy_argv (event->argv);
	memset (event, 0, sizeof (*event));
}


/** Add a socket to the registry.
    @param socket_fd The file descriptor of the socket being added.
    @param type Type of socket (service, connection, user...)
    @param thing Pointer of type indicated by 'type'.
    @return true on success, false on failure */
bool fb_register (int socket_fd, FB_SOCKETTYPE type, void *thing) {
	/* Initialize the select state if this is the first registration */
	if (maxsockets == 0) {
		memset (&select_state, 0, sizeof (select_state));
	}
	
	/* We're limited by select(2), unless we want to do unsupported things */
	if (socket_fd >= FD_SETSIZE) {
		return false;
	}
	
	/* Make sure we've got enough space in the socket registry */
	if (maxsockets <= socket_fd) {
		/* maxsockets affects the select(), so used a balanced
		 approach to growing the collection */
		int newsize = maxsockets + (maxsockets / 4) + 10;
        if (newsize <= socket_fd) {
            newsize = socket_fd + 10;
        }
		if (newsize > FD_SETSIZE) {
			newsize = FD_SETSIZE;
		}
		FB_SOCKET_DATA **newsockets = realloc (sockets, newsize * sizeof (FB_SOCKET_DATA *));
		if (newsockets == NULL) {
            fb_perror ("realloc");
			return false;
		}
		/* Zero out the new portion of the collection */
		memset (newsockets + maxsockets, 0, (newsize - maxsockets) * sizeof (FB_SOCKET_DATA *));
		sockets = newsockets;
		maxsockets = newsize;
	}
	
	/* Insert the socket into the registry. */
	assert (sockets [socket_fd] == NULL);		/* There shouldn't be anything there yet. */
	if ((sockets [socket_fd] = malloc (sizeof (FB_SOCKET_DATA)))) {
		sockets [socket_fd]->thingie.user = thing;
		sockets [socket_fd]->socket = socket_fd;
		sockets [socket_fd]->type = type;
		FD_SET (socket_fd, &select_state[ACTION_READING]); /* Enable input */
        if (socket_fd >= activesockets) {
            activesockets = socket_fd + 1;
        }
		return true;
	}
    fb_perror ("malloc");
	return false;
}

/** Remove a socket from the registry.
    Always succeeds, unless there's a bug.
    @param socket_fd the file descriptor to unregister */
void fb_unregister (int socket_fd) {
	assert (socket_fd > 0 && socket_fd < maxsockets);	/* We shouldn't be freeing sockets that don't fit in the registry. */
	assert (sockets [socket_fd]);	/* The socket to be freed should exist in the registry. */
	
	/* Turn off listening to it and cancel any pending processing */
	int i;
	for (i = 0; i < ACTION_COUNT; i++) {
		FD_CLR(socket_fd, &select_state[i]);
		FD_CLR(socket_fd, &last_state[i]);
	}
	
	free (sockets [socket_fd]);
	sockets [socket_fd] = NULL;

    /* Stop polling inactive sockets to reduce impact on select() */
    while (activesockets > 0 && sockets [activesockets - 1] == NULL) {
        activesockets--;
    }
}


/** @internal
    Enable/disable the flags for selecting, or the buffering flags.
    @param socket_fd the file descriptor to set flags for
    @param group which flags to adjust (read, write, ...)
    @param enable true to enable the flag, false to clear it */
void fb_set_socket_select_flags (int socket_fd, ACTION group, bool enable) {
	assert (socket_fd > 0 && socket_fd < activesockets);
	assert (sockets [socket_fd] != NULL);

	if (sockets [socket_fd]) {
		if (enable) {
			FD_SET(socket_fd, &select_state[group]);
		} else {
			FD_CLR(socket_fd, &select_state[group]);
		}
	}
}



/** @internal
    Enable/disable writing on a socket.
    This is used to remember TLS data buffering.
    @param socket_fd the file descriptor to apply flags to
    @param enable true if TLS is buffering, false if it is not. */
void fb_set_buffering (int socket_fd, bool enable) {
    fb_set_socket_select_flags (socket_fd, ACTION_BUFFERING, enable);
    if (enable) {
        tls_currently_buffering = true;
    }
}

/** @internal
    Enable/disable writing on a socket.
    This is used within Football, enabled when fresh data is queued for a socket and
    disabled when the buffer is empty.
    @param socket_fd the file descriptor to apply flags to
    @param enable true to enable write-polling on the socket */
void fb_set_writable (int socket_fd, bool enable) {
    fb_set_socket_select_flags (socket_fd, ACTION_WRITING, enable);
}

/** @internal
    Enable/disable reading on a socket.
    @param socket_fd the file descriptor to apply flags to
    @param enable true to enable reads from the socket, false to disable */
void fb_set_readable (int socket_fd, bool enable) {
    fb_set_socket_select_flags (socket_fd, ACTION_READING, enable);
}

/** Public interface to enable/disable reading on a connection.
    @param connection - the connection to adjust.
    @param input true to enable reading, false to disable. */
void fb_accept_input (FB_CONNECTION *connection, bool input) {
	assert (connection);
	assert (connection->state == FB_SOCKET_STATE_OPEN);
	
	fb_set_readable (connection->socket, input && connection->state <= FB_SOCKET_STATE_OPEN);
    if (input) {
        /* Set buffering flag if TLS is buffering data for this socket. */
        if (connection->encrypted && connection->state > FB_SOCKET_STATE_TLS_HANDSHAKE &&
                                     connection->state <= FB_SOCKET_STATE_OPEN) {
#ifdef WORKING_LIBGNUTLS
            fb_set_buffering (connection->socket, gnutls_record_check_pending (connection->tls));
#endif
        }
    } else {
        /* Clear buffering flag so we don't try to read that way either. */
        fb_set_buffering (connection->socket, false);
    }
}

/** @internal
    Called by the poll routine, this function takes a socket and an action
    and processes it, either inline or by calling another function to do
    the work.
    @param socket_fd file descriptor of a socket that is ready/wants attention.
    @param action indicates if socket is ready for reading, writing, etc.
    @return an event, when appropriate.  
            If things are handled completely internally, returns NULL. */
FB_EVENT *fb_process_event (int socket_fd, ACTION action) {
	static FB_EVENT event;
	assert (socket_fd >= 0 && socket_fd < activesockets);
	assert (action >= 0 && action < ACTION_SELECT_COUNT);
	assert (sockets [socket_fd]);

	/* Look up the type of the event originator */
	FB_SOCKET_DATA *socket_data = sockets [socket_fd];
	if (socket_data == NULL) {
		return NULL;
	}

	/* Release any even resources, then clear it out */
    fb_dispose_event (&event);

	/* Initialize the event in case we need it */
	event.magic = FB_SOCKTYPE_EVENT;
	event.socket = socket_fd;

	switch (socket_data->type) {
		case FB_SOCKTYPE_SERVICE:
			switch (action) {
				case ACTION_READING:
					return fb_new_connect (&event, socket_data->thingie.service);
				default:
					assert (0);
					break;
			}
			break;
		case FB_SOCKTYPE_CONNECTION:
			event.context = socket_data->thingie.connection->context;
			event.connection = socket_data->thingie.connection;
			event.service = event.connection->service;
			switch (action) {
				case ACTION_READING:
                {
                    FB_EVENT *e = fb_read_input (&event, socket_data->thingie.connection);
#ifdef WORKING_LIBGNUTLS
                    socket_data = sockets [socket_fd];
                    if (socket_data &&
                        socket_data->thingie.connection->encrypted) {
                        fb_set_buffering (socket_fd,
                                          socket_data->thingie.connection->state <= FB_SOCKET_STATE_OPEN &&
                                          FD_ISSET (socket_fd, &select_state [ACTION_READING]) &&
                                          gnutls_record_check_pending (socket_data->thingie.connection->tls));
                    }
#endif
                    return e;
                }
				case ACTION_WRITING:
					return fb_send_output (&event, socket_data->thingie.connection);
				default:
					assert (0);
					break;
			}
			break;
		case FB_SOCKTYPE_USER:
			event.context = socket_data->thingie.user;
			switch (action) {
				case ACTION_READING:
					event.type = FB_EVENT_READABLE;
					return &event;
				case ACTION_WRITING:
					event.type = FB_EVENT_WRITABLE;
					return &event;
				case ACTION_FAULTING:
					event.type = FB_EVENT_FAULTING;
					return &event;
				default:
					assert (0);
					break;
			}
			break;
		default:
            fb_log (FB_WHERE (FB_LOG_ERROR), "Invalid socket type %d in switch", socket_data->type);
			assert (0);
			return NULL;
	}
	fb_log (FB_WHERE (FB_LOG_ERROR), "Event type not handled: type=%d, action=%d", socket_data->type, action);
	return NULL;
}



/** @internal
    Poll the stuff in the registry.
    This is the worker function; the four exposed functions with various timeout
    mechanisms follow.
    @param timeout Duration to poll, or NULL for indefinite wait.
    @return an event structure, or NULL on failure. */
static FB_EVENT *fb_poll_for (struct timeval *timeout) {
pollagain:;
	static FB_EVENT event; /* Reusable timeout event */
	static int events_remaining = 0;
	static ACTION process_action = 0;
	static int process_fd = 0;
	
	/* Something should have been registered by now. */
	assert (maxsockets > 0);
    if (activesockets <= 0) {
        /* If there are no registrations left, return. */
        fb_log (FB_WHERE (FB_LOG_ERROR), "No sockets registered.");
        return NULL;
    }

    if (queued_event) {
        FB_EVENT *temp = queued_event;
        queued_event = NULL;
        return temp;
    }
	/* See if there's any reaping queued; if so, do it. */
	if (reapq) {
        memset (&event, 0, sizeof (event));
        event.type = FB_EVENT_STOPPED;
        /* Free services that have already been reap notified */
        while (reapq && reapq->shutdown_event_done) {
            FB_SERVICE *current = reapq;
            reapq = reapq->next_reap;
            fb_destroy_service (current);
            fb_free_freelists ();
        }
        if (reapq) {
            /* Send STOPPED event with valid service pointer */
            reapq->shutdown_event_done = true;
            event.service = reapq;
            return &event;
        }
        /* After last service is reaped, return STOPPED event with no service. */
        if (activesockets == 0) {
            return &event;
        }
	}
	
	/* See if it's time to refill the coffers, so-to-speak */
	if (events_remaining == 0) {
		/* Reset the event searching loops */
		process_action = 0;
		process_fd = 0;
		
		/* Create a fresh copy of the selector masks */
		int i;
		for (i = 0; i < ACTION_SELECT_COUNT; i++) {
			FD_COPY(&select_state[i], &last_state [i]);
		}
		/* Select, repeating if we get an interrupted system call */
		do {
            struct timeval zero;
            zero.tv_sec = 0;
            zero.tv_usec = 0;
			events_remaining = select (maxsockets, &last_state [ACTION_READING],
									   &last_state [ACTION_WRITING], &last_state [ACTION_FAULTING],
                                       tls_currently_buffering ? &zero : timeout);
		} while (!tls_currently_buffering && events_remaining < 0 && errno == EINTR);

        /* Add TLS's buffered reads into select's read flags */
        if (tls_currently_buffering) {
            tls_currently_buffering = false;
            for (i = 0; i < maxsockets; i++) {
                if ( FD_ISSET(i, &select_state [ACTION_BUFFERING]) &&
                    !FD_ISSET(i, &last_state [ACTION_READING])) {
                    events_remaining++;
                    FD_SET(i, &last_state [ACTION_READING]);
                }
            }
        }

		/* Check for/handle (don't handle) errors */
		if (events_remaining < 0) {
			events_remaining = 0;
            fb_perror ("select");
			return NULL;
		}
		/* Check for/handle timeout */
		if (events_remaining == 0) {
			/* We'll just keep reusing this, just clearing it out each time. */
			memset (&event, 0, sizeof (event));
			event.magic = FB_SOCKTYPE_EVENT;
			event.type = FB_EVENT_TIMEOUT;
			return (&event);
		}
	}

	/* Search the mask returned by select to find what needs attention */
	while (process_action < ACTION_SELECT_COUNT) {
		while (process_fd < maxsockets) {
			if (FD_ISSET (process_fd, &last_state [process_action])) {
				/* This needs attention.  Count and process it. */
				events_remaining--;
				FB_EVENT *fd_event = fb_process_event (process_fd, process_action);
				if (fd_event) {
					/* An event resulted from the processing. Bump to the */
					/* next socket so we don't repeat, then return the event. */
					if (++process_fd >= maxsockets) {
						process_fd = 0;
						process_action++;
					}
					return (fd_event);
				}
#ifndef NDEBUG
				/* If there are no more events, stop looking.  But in test mode,
				 continue to make sure we counts confirm good behavior. */
				if (events_remaining <= 0) goto pollagain;
#endif
			}
			process_fd++;
		}
		process_fd = 0;
		process_action++;
	}
	if (events_remaining != 0) {
        fb_log (FB_WHERE (FB_LOG_WARNING), "%d event(s) not found", events_remaining);
	}
	events_remaining = 0;
	goto pollagain; /* Tail recursion would probably collapse and work too */
}

/** Quick poll with no waiting.
    @return An event. */
FB_EVENT *fb_poll (void) {
	struct timeval zero;
	zero.tv_sec = 0;
	zero.tv_usec = 0;
	return fb_poll_for (&zero);
}
/** Poll with a duration.
    @param timeout in seconds.
    @return An event. */
FB_EVENT *fb_poll_with_timeout (double timeout) {
	struct timeval _timeout;
	_timeout.tv_sec = timeout;
	_timeout.tv_usec = (unsigned int) ((long) (timeout * 1000000) % 1000000);
	return fb_poll_for (&_timeout);
}
/** Poll indefinitely.
   @return An event. */
FB_EVENT *fb_wait (void) {
	return fb_poll_for (NULL);
}
/** Poll with a timeout at a specific time.
    @param untilwhen Time at which timeout should occur.
    @return An event.*/
FB_EVENT *fb_poll_until (time_t untilwhen) {
	struct timeval timeout;
	time_t now = time(NULL);
	if (now == -1) { /* According to the man page time() can actually fail */
        fb_perror ("time");
		timeout.tv_sec = 1; /* *shrug*, there's really bigger problems if you don't know what time it is */
	} else {
		long timeLeft = untilwhen - now;
		timeout.tv_sec = (timeLeft < 0) ? 0 : timeLeft;
	}
	timeout.tv_usec = 0;
	return fb_poll_for (&timeout);
}


