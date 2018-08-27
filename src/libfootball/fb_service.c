///
/// Football service and connection creation/destruction.
/// @file       fb_service.c - Football socket abstraction layer
/// @author     Perette Barella
/// @date       2012-03-03
/// @copyright  Copyright 2012–2014 Devious Fish. All rights reserved.
///

#include <config.h>

#if !defined(__FreeBSD__) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 1 /* fileno,fdopen() */
#endif
#ifndef __FreeBSD__
#define _DEFAULT_SOURCE /* strdup */
#endif

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>

#include "fb_service.h" // Needs to precede gnutls to determine workability

#ifdef WORKING_LIBGNUTLS
#include <gnutls/gnutls.h>
#endif


static int fb_open_service_count = 0;

/** Indicate whether there are any services open.
    @return true if services exist, false otherwise. */
bool fb_services_are_open (void) {
    return fb_open_service_count > 0;
}


/** @internal
    Initialize and set up a listening socket.
    @return true on success, false on failure */
static bool fb_setup_socket (FB_SERVICE *service, FB_SOCKETID which) {
#ifndef WORKING_LIBGNUTLS
    /* That's #if_n_def: skip socket creation for encrypted socket when not available. */
    if (fb_encrypted_socket(which)) {
        fb_log (FB_WHERE (FB_LOG_WARNING), "TLS support not available.  Install/update GNUTLS and rebuild, or review config.log for missing prerequisites.");
        return false;
    }
#endif
    bool ipv6 = fb_ip6_socket (which);
    in_port_t port = fb_http_socket (which) ?
                    (fb_encrypted_socket (which) ? service->options.https_port : service->options.http_port) :
                    service->options.line_port;
    if (!port) {
        return false;
    }
#ifdef HAVE_IPV6
	if ((service->socket [which] = socket (ipv6 ? PF_INET6 : PF_INET, SOCK_STREAM, 0)) >= 0) {
#else
    if (ipv6) {
        fb_log (FB_WHERE (FB_LOG_WARNING), "IPV6 support not available.  Check HAVE_IPV6 in config.log if it should be.");
        return false;
    }
    if ((service->socket [which] = socket (PF_INET, SOCK_STREAM, 0)) >= 0) {
#endif
        int on = 1;
		/* We got a socket, bind and listen on it. */
        if (ipv6) {
#ifdef HAVE_IPV6
            service->address [which].ip6.sin6_family = PF_INET6;
            service->address [which].ip6.sin6_addr = in6addr_any;
            service->address [which].ip6.sin6_port = htons (port);
#else
            assert (0);
#endif
        } else {
            service->address [which].ip4.sin_family = PF_INET;
            service->address [which].ip4.sin_addr.s_addr = INADDR_ANY;
            service->address [which].ip4.sin_port = htons (port);
        }
		if (setsockopt(service->socket [which], SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		{
            fb_perror ("setsockopt(SO_REUSEADDR)");
			/* That's annoying but not critical, so keep going. */
		}
#ifdef HAVE_IPV6
		if (bind (service->socket [which],
				  (struct sockaddr*) &(service->address [which]),
                  ipv6 ? sizeof (service->address [which].ip6) : sizeof (service->address [which].ip4)) >= 0) {
#else
        if (bind (service->socket [which],
                    (struct sockaddr*) &(service->address [which]), sizeof (service->address [which].ip4)) >= 0) {
#endif
			if (listen (service->socket [which], service->options.queue_size) >= 0) {
				if (fb_register (service->socket [which], FB_SOCKTYPE_SERVICE, service)) {
					return true;
				}
				/* Errors already announced by fb_register */
			}
		} else {
            fb_log (FB_WHERE (FB_LOG_ERROR), "bind: %s (%s port %d)",
                    strerror (errno), fb_ip6_socket (which) ? "IP6" : "IP4", (int) port);
		}
		close (service->socket [which]);
		service->socket [which] = 0;
	} else {
        fb_perror ("socket");
        // Element must be zero (not -1)
        service->socket [which] = 0;
	};
	return false;
}


/** Create a new service and initialize its listeners.
    Subsequent changes made to the options do not effect behavior.
    @param options Options for the new service's behavior.
    @return a pointer to the service, or NULL on failure. */
FB_SERVICE *fb_create_service (const FB_SERVICE_OPTIONS *options) {
    assert (options->line_port || options->http_port ||
            options->https_port || options->transfer_only);
    if (options->line_port == 0 && options->http_port == 0 &&
        options->https_port == 0 && !options->transfer_only) {
        return NULL;
    }
    /* Either we're a root, or the parent's the root.  No multilevel. */
    if (options->parent) {
        assert (!options->parent->options.parent);
        assert (strcasecmp (options->name, options->parent->options.name) != 0);
    }

	/* Allocate and initialize memory for the service */
	FB_SERVICE *service;
	if ((service = calloc (1, sizeof (*service)))) {
        service->options = *options;
        service->options.name = options->name ? strdup (options->name) : NULL;
        service->options.greeting = strdup (options->greeting ? options->greeting
                                            : options->parent ? options->parent->options.greeting
                                            : "HELO");
        const char *servedir = ((options->serve_directory || !options->parent)
                                ? options->serve_directory
                                : options->parent->options.serve_directory);
        service->options.serve_directory = servedir ? strdup (servedir) : NULL;
        if ((service->options.name || !options->name) &&
            (service->options.serve_directory || !options->serve_directory) &&
            service->options.greeting) {
            service->type = FB_SOCKTYPE_SERVICE;

            /* Initialize and set up the sockets */
            int successes = 0;
            for (int i = 0; i < FB_SOCKET_COUNT; i++) {
                successes += fb_setup_socket (service, i);
            }

            if (successes > 0 || options->transfer_only) {
                service->state = FB_SOCKET_STATE_OPEN;
                fb_open_service_count++;
                if (options->parent) {
                    service->next_child = options->parent->next_child;
                    options->parent->next_child = service;
                    /* If we're using parents, everybody needs a name. */
                    assert (options->name);
                    assert (options->parent->options.name);
                }
                return (service);
            }
            /* Errors already announced by fb_setup_socket */
        }
        free (service->options.name);
        free (service->options.greeting);
        free (service->options.serve_directory);
        free (service);
    } else {
        fb_perror ("calloc");
	}
	return NULL;
}


/** @internal
    Destroy a service's resources.
    Abruptly closes any remaining connections, frees them,
    closes listener sockets and frees service.
    @param service the service to terminate.
    @return Nothing. */
void fb_destroy_service (FB_SERVICE *service) {
	/* Close all the connections first */
	assert (service);
	assert (service->connection_count == 0);
	while (service->connection_count > 0) {
		fb_destroy_connection (service->connections [0]);
	}
	/* Close our listeners */
    for (FB_SOCKETID id = 0; id < FB_SOCKET_COUNT; id++) {
        if (service->socket [id]) {
            fb_unregister (service->socket [id]);
            close (service->socket [id]);
        }
	}
    if (service->options.parent) {
        /* Remove dead child from linked list. */
        FB_SERVICE *node = service->options.parent;
        while (node->next_child != service) {
            node = node->next_child;
            assert (node);
        }
        node->next_child = node->next_child->next_child;
    } else {
        /* This might be a parent.  Orphan all children. */
        FB_SERVICE *child = service->next_child;
        while (child) {
            FB_SERVICE *next = child->next_child;
            child->options.parent = NULL;
            child->next_child = NULL;
            child = next;
        }
    }
    free (service->options.greeting);
    free (service->options.serve_directory);
    free (service->options.name);
	free (service->connections); /* Per man page, freeing null is okay */
	free (service);
    fb_open_service_count--;
}


/** Initiate closure of a service.
    @return Nothing. */
void fb_close_service (FB_SERVICE *service) {
	unsigned int i;
	assert (service);
	assert (service->state == FB_SOCKET_STATE_OPEN);
	service->state = FB_SOCKET_STATE_CLOSING;
	for (i = 0; i < service->connection_count; i++) {
        fb_close_connection (service->connections[i]);
	}
	/* Stop listening so we don't accept new connections */
    for (FB_SOCKETID id = 0; id < FB_SOCKET_COUNT; id++) {
        if (service->socket [id]) {
            fb_set_readable (service->socket [id], false);
        }
	}
	if (service->connection_count == 0) {
		/* If there's no connections left, schedule for reaping. */
		fb_schedule_reap (service);
	}
}



/** @internal
    Prepare a new connection.
    Make space in the service's connection array, allocate
    a connection and (if requested in service options) a user context.
    @param service the service for the new connection.
    @return a fully-allocated and zero-initialized FB_CONNECTION. */
static FB_CONNECTION *fb_new_connection (FB_SERVICE *service) {
	/* Expand the connection list for this service if needed. */
	assert (service);
	assert (service->connection_count <= service->connections_size); /* If this is off, something already went wrong */
    if (!fb_expandcalloc ((void **) &service->connections, &service->connections_size,
                          service->connection_count + 1, sizeof (FB_CONNECTION *))) {
        fb_perror ("fb_expandcalloc");
        return NULL;
	}
	/* Allocate the connection, initialize it, and add a context if necessary */
	FB_CONNECTION *connection = calloc (1, sizeof (*connection));
	if (connection) {
		connection->type = FB_SOCKTYPE_CONNECTION;
		connection->service = service;
		/* If a context isn't needed, we're good */
		if (service->options.context_size == 0) {
			return (connection);
		}
		connection->context = calloc (1, service->options.context_size);
		if (connection->context) {
			return (connection);
		}
		free (connection);
	}
    fb_perror ("calloc");
	return NULL;
}	


/** @internal
    Initialize the TLS stuff for a new connection.
    @param connection The connection to initialize.
    @return true on success, false on error. */
#ifdef WORKING_LIBGNUTLS
static bool fb_setup_connection_tls (FB_CONNECTION *connection) {
    gnutls_certificate_client_credentials creds = fb_get_tls_credentials ();
    if (!creds) {
        fb_log (FB_WHERE (FB_LOG_ERROR), "TLS credentials not set.  Call fb_init_tls_support().");
        return false;
    }
    char *func="gnutls_init";
    int status = gnutls_init (&connection->tls, GNUTLS_SERVER);
    if (status == GNUTLS_E_SUCCESS) {
        func = "gnutls_priority_set";
        status = gnutls_priority_set(connection->tls, fb_get_tls_priorities ());
        if (status == GNUTLS_E_SUCCESS) {
            func = "gnutls_credentials_set";
            status = gnutls_credentials_set (connection->tls, GNUTLS_CRD_CERTIFICATE, creds);
            if (status == GNUTLS_E_SUCCESS) {
                /* No return value from this next guy... */
                gnutls_certificate_server_set_request (connection->tls, GNUTLS_CERT_IGNORE);
                return true;
            }
        }
    }
    fb_log (FB_WHERE (FB_LOG_TLS_ERROR), "%s: %s", func, gnutls_strerror(status));
    return false;
}
#endif

/** @internal
    Accept a connection.
    @param service The service on which a connection is arriving.
    @param id Identifies the arrival socket (line, HTTP, TLS, IP4 vs IP6)
    @return a pointer to a new connection, or NULL on failure. */
FB_CONNECTION *fb_accept_connection (FB_SERVICE *service, FB_SOCKETID id) {
	assert (service);
	FB_CONNECTION *connection = fb_new_connection (service);
	if (!connection) {
		/* If there's not enough memory to store the connection, just reject it */
		/* TODO: REJECT CONNECTION */
		return NULL;
	}
	/* Allocate and initialize memory for the service */
	connection->domain = fb_ip6_socket (id) ? PF_INET6 : PF_INET;
    connection->http = fb_http_socket (id);
    connection->encrypted = fb_encrypted_socket (id);
	socklen_t addr_size = (socklen_t) sizeof (connection->origin);
    connection->state = connection->encrypted ? FB_SOCKET_STATE_TLS_HANDSHAKE :
                        connection->http || service->options.greeting_mode == FB_GREETING_REQUIRE ||
                                            service->options.greeting_mode == FB_GREETING_FALLBACK ?
                        FB_SOCKET_STATE_GREETING : FB_SOCKET_STATE_OPEN;

	/* Accept the connection */
#ifdef WORKING_LIBGNUTLS
    /* Depend on initializing TLS stuff first */
    if (connection->encrypted && !fb_setup_connection_tls (connection)) {
        /* Errors already reporte by fb_setup_connection_tls */
    } else
#endif
	if ((connection->socket = accept (service->socket [id],
									  (struct sockaddr *) &(connection->origin), &addr_size)) >= 0) {
        /* Add to the connection list */
        service->connections [service->connection_count++] = connection;
        /* put the socket in non-blocking mode */
        if (fcntl (connection->socket, F_SETFL, fcntl (connection->socket, F_GETFL) | O_NONBLOCK) == -1) {
            fb_perror ("fcntl");
        }
        /* Disable the dreaded killer sigpipe */
#ifdef HAVE_SO_NOSIGPIPE
        int option_value = 1; /* We're setting NOSIGPIPE to ON */
        if (setsockopt (connection->socket, SOL_SOCKET, SO_NOSIGPIPE, &option_value, sizeof (option_value)) < 0) {
            fb_perror ("setsockopt(,,SO_NOSIGPIPE)");
        }
#endif
#ifdef WORKING_LIBGNUTLS
        if (connection->encrypted) {
            gnutls_transport_set_int (connection->tls, connection->socket);
        }
#endif
        return (connection);
	} else {
        fb_perror ("accept");
	}
    free (connection->context);
	free (connection);
	return NULL;
}


/** @internal
    Remove a connection from a service's connection list.
    @param service The service to remove from.
    @param connection The connection to remove.
 */
static void fb_remove_connection_from_service (FB_SERVICE *service, FB_CONNECTION *connection) {
    /* Take the connection out of the service's list */
    unsigned int i;
    for (i = 0; i < service->connection_count && service->connections [i] != connection; i++)
    /* nothing */;
    assert (i < service->connection_count); /* If we didn't find it, there's a bug */
    service->connection_count--;
    unsigned int j;
    for (j = i; j < service->connection_count; j++) {
        service->connections [j] = service->connections [j+1];
    }
}


/** Transfer a connection to a new service.
    @param connection The connection to transfer.
    @param service The service to reassign it to. */
bool fb_transfer (FB_CONNECTION *connection, FB_SERVICE *service) {
    assert (connection);
    assert (service);

    /* Make sure there's room in the destination's connection array */
    if (!fb_expandcalloc ((void **) &service->connections, &service->connections_size,
                          service->connection_count + 1, sizeof (FB_CONNECTION *))) {
        fb_perror ("fb_expandcalloc");
        return false;
    }

    /* Remove connection from the original service's array */
    fb_remove_connection_from_service (connection->service, connection);

    /* Add the connection to the destination service's array */
    service->connections [service->connection_count++] = connection;
    connection->service = service;
    return true;
}

/** @internal
    Transfer a connection to a related service.
    @param connection The connection to transfer.
    @param name The name of the service to reassign it to. */
bool fb_transfer_by_name (FB_CONNECTION *connection, const char *name) {
    assert (connection);
    assert (name);

    FB_SERVICE *service = connection->service->options.parent;
    if (!service) {
        service = connection->service;
    }
    while (service) {
        if (strcasecmp (service->options.name, name) == 0) {
            return fb_transfer(connection, service);
        }
        service = service->next_child;
    }
    return false;
}

/** @internal
    Close and destroy a connection.
    @return Nothing. */
void fb_destroy_connection (FB_CONNECTION *connection) {
	assert (connection);
	
    fb_remove_connection_from_service (connection->service, connection);

    /* Release TLS resources */
#ifdef WORKING_LIBGNUTLS
    if (connection->encrypted) {
        gnutls_bye (connection->tls, GNUTLS_SHUT_WR);
        gnutls_deinit (connection->tls);
    }
#endif

	/* Close the socket and free resources */
    fb_unregister(connection->socket);
    if (connection->file) {
        fclose (connection->file);
    } else {
        close (connection->socket);
    }
	free (connection->filename);
	free (connection->context);
    free (connection->in.message);
    fb_queue_destroy(&connection->assembly);
    fb_queue_destroy(&connection->out);
    fb_destroy_httprequest (&connection->request);
    fb_log (FB_WHERE (FB_LOG_CONN_STATUS), "#%d: Connection terminated.", connection->socket);
	free (connection);
}



/** Initiate connection closure.
    If the socket is in an open state, change its state and enable
    writes.  The next write attempt will generate a close event.
    If the connection is already on its way out, leave it alone.
    @param connection the connection to close */
void fb_close_connection (FB_CONNECTION *connection) {
	assert (connection);
	
	if (connection->state <= FB_SOCKET_STATE_OPEN) {
		connection->state = FB_SOCKET_STATE_FLUSHING;
	}
    /* Ignore further input: if service is closing, this avoids a connection
     requesting individual closure and causing a duplicate close. */
    fb_set_writable (connection->socket, true);
    fb_set_readable (connection->socket, false);
    fb_set_buffering (connection->socket, false);
}



/** Make a connection that reads from a file.
    @service the service to register the connection with
    @filename the file to read from
    @return a pointer to the connection, or NULL on failure. */
FB_EVENT *fb_accept_file (FB_SERVICE *service, char *filename) {
	assert (service);
	assert (filename && *filename);
	static FB_EVENT event;
	/* Allocate and initialize memory for the service */
	memset (&event, 0, sizeof (event));
	event.magic = FB_SOCKTYPE_EVENT;
	event.type = FB_EVENT_CONNECT;
	event.service = service;
	/* Allocate a new connection and fill it in; create an event for the new connection. */
	event.connection = fb_new_connection (service);
    event.connection->state = FB_SOCKET_STATE_OPEN;
	if (event.connection) {
		if ((event.connection->filename = strdup (filename))) {
			/* All the allocations were successful, so open the file. */
			if ((event.connection->file = fopen (filename, "r"))) {
				/* select() and stdio buffering don't play nice... shut off buffering */
				setbuf (event.connection->file, NULL);
				event.context = event.connection->context;
				event.connection->socket = fileno (event.connection->file);
				event.socket = event.connection->socket;
				if (fb_register (event.connection->socket, FB_SOCKTYPE_CONNECTION, event.connection)) {
					/* Add to the connection list */
					service->connections [service->connection_count++] = event.connection;
                    fb_log (FB_WHERE (FB_LOG_CONN_STATUS), "#%d: New file connection for %s",
                            event.connection->socket, filename);
					return &event;
				}
				fclose (event.connection->file);
			} else {
				fb_log (FB_WHERE (FB_LOG_ERROR), "%s: %s", filename, strerror (errno));
			}
			free (event.connection->filename);
		} else {
            fb_perror ("strdup");
		}
        free (event.connection->context);
		free (event.connection);
	}
	return NULL;
}


/** Create a new iterator.
    Create and initialize a new iterator for a service.
    @see fb_destroy_iterator().
    @return an iterator or NULL on error. */
FB_ITERATOR *fb_new_iterator (FB_SERVICE *service) {
	assert (service);
	FB_ITERATOR *it = calloc (sizeof (FB_ITERATOR), 1);
	if (it) {
		it->service = service;
		it->iteration = service->connection_count;
	} else {
        fb_perror ("calloc");
	}
	return (it);
}


/** Get the next iteration.
    @see FB_EVENTTYPE
    @param it The iterator
    @return An event, or NULL if there are no more connections. */
FB_EVENT *fb_iterate_next (FB_ITERATOR *it) {
	static FB_EVENT event;
	assert (it);
	assert (it->service);
	assert (it->iteration >= 0);
	if (it->iteration > 0) {
		it->iteration--;
        if (it->service->connections [it->iteration]->state < FB_SOCKET_STATE_OPEN) {
            return fb_iterate_next (it);
        }
		memset (&event, 0, sizeof (event));
		event.magic = FB_SOCKTYPE_EVENT;
		event.service = it->service;
		event.connection = it->service->connections [it->iteration];
		event.type = event.connection->state == FB_SOCKET_STATE_OPEN ? FB_EVENT_ITERATOR : FB_EVENT_ITERATOR_CLOSE;
		event.socket = event.connection->socket;
		event.context = event.connection->context;		
		return (&event);
	}
	return NULL;
}


/** Destroy an iterator and release its resources.
    @param it The iterator */
void fb_destroy_iterator (FB_ITERATOR *it) {
	assert (it);
	free (it);
}
