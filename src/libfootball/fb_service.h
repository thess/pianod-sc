///
/// Football private declarations.
/// @file       fb_service.h - Football socket abstraction layer
/// @author     Perette Barella
/// @date       2012-03-03
/// @copyright  Copyright 2012–2014 Devious Fish. All rights reserved.
///


#ifndef __football__service__
#define __football__service__

#include <config.h>
#if defined (HAVE_LIBGNUTLS) && defined (HAVE_GNUTLS_TRANSPORT_SET_INT2)
#define WORKING_LIBGNUTLS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef WORKING_LIBGNUTLS
#include <gnutls/gnutls.h>
#endif

#include "fb_public.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Logging facilities */
typedef enum fb_log_types_t {
	FB_LOG_ERROR = 0, /* Nonmaskable */
	FB_LOG_WARNING = 0x01,
    FB_LOG_PARSER = 0x04,
    FB_LOG_IO_TRACE = 0x08,
    FB_LOG_CONN_STATUS = 0x10,
    FB_LOG_CONN_ERROR = 0x20,
    FB_LOG_TLS_STATUS = 0x100,
    FB_LOG_TLS_ERROR = 0x200,
    FB_LOG_HTTP_STATUS = 0x1000,
    FB_LOG_HTTP_ERROR = 0x2000,
    FB_LOG_HTTP_TRAFFIC = 0x4000,
} FB_LOG_TYPE;
/** Use FB_WHERE to send the log type; this macro includes the file, line and function
    when NDEBUG is not set. */
#ifdef NDEBUG
#define FB_WHERE(level) (level)
#else
#define FB_WHERE(level) __FILE__, __LINE__, __func__, (level)
#endif
/** Log a message with a form akin to perror */
#define fb_perror(errfunc) fb_log(FB_WHERE (FB_LOG_ERROR), (errfunc ": %s"), strerror (errno))

/** Magic numbers used to differentiate parameter types sent to fb_fprintf. */
typedef enum fb_socket_type_t {
	FB_SOCKTYPE_SERVICE = 0x3692, /**< Random numbers */
	FB_SOCKTYPE_CONNECTION = 0x5285,
	FB_SOCKTYPE_USER = 0xa9f7,
	FB_SOCKTYPE_EVENT = 0xbd53
} FB_SOCKETTYPE;

/** Connection states */
typedef enum fb_socket_state_t {
    FB_SOCKET_STATE_TLS_HANDSHAKE, /**< Connection has not yet completed TLS handshake. */
    FB_SOCKET_STATE_GREETING, /**< HTTP/TLS connection is awaiting a request. */
    FB_SOCKET_STATE_GATHERING_HEADER, /**< HTTP/HTTPS request in process, still reading headering */
	FB_SOCKET_STATE_OPEN, /**< Line-oriented or WebSocket session in process */
	FB_SOCKET_STATE_FLUSHING, /**< Line-oriented or WebSocket session is closing, flushing output */
	FB_SOCKET_STATE_CLOSING /**< Line-oriented or WebSocket connection has finished flushing */
} FB_SOCKETSTATE;

typedef enum fb_socketid_t {
    FB_SOCKET_LINE_IP4,
    FB_SOCKET_LINE_IP6,
    FB_SOCKET_HTTP_IP4,
    FB_SOCKET_HTTP_IP6,
    FB_SOCKET_HTTPS_IP4,
    FB_SOCKET_HTTPS_IP6,
    FB_SOCKET_COUNT
} FB_SOCKETID;

typedef union fb_socketaddr_t {
    struct sockaddr_in ip4;
#ifdef HAVE_IPV6
	struct sockaddr_in6 ip6;
#endif
} FB_SOCKETADDR;

#define fb_http_socket(id) ((id) == FB_SOCKET_HTTP_IP4 || (id) == FB_SOCKET_HTTP_IP6 || \
                            (id) == FB_SOCKET_HTTPS_IP4 || (id) == FB_SOCKET_HTTPS_IP6)
#define fb_ip6_socket(id) ((id) == FB_SOCKET_LINE_IP6 || (id) == FB_SOCKET_HTTP_IP6 || \
                           (id) == FB_SOCKET_HTTPS_IP6)
#define fb_encrypted_socket(id) ((id) == FB_SOCKET_HTTPS_IP4 || (id) == FB_SOCKET_HTTPS_IP6)

struct fb_service_t {
	FB_SOCKETTYPE type; /**< Magic number so we know this is a service in fb_fprintf */
	FB_SOCKETSTATE state;	/**< Is the service open or closing? */
    FB_SERVICE_OPTIONS options; /**< Behavior options */
	int socket [FB_SOCKET_COUNT]; /**< Socket file descriptors */
	FB_SOCKETADDR address [FB_SOCKET_COUNT]; /**< Socket address information */
	size_t connection_count;	/**< Number of active connections in the collection */
	size_t connections_size;	/**< Total number of slots in the collection */
	struct fb_connection_t **connections;	/**< Connections to services */
	struct fb_service_t *next_reap; /**< For reap queue */
    bool shutdown_event_done; /**< Return shutdown event once. */
    struct fb_service_t *next_child; /**< Children or next child */
    void *relatedObject; /**< C++ object for this service */
};

/* Nomenclature:
   Message - Contains the message data and a use count, allowing
             use in several Qs for efficiency.
   Q a/k/a MessageList - Per-connection linked list of Messages.
   Queue - header data structure for Q MessageList.
 */
/** Message structure.  May appear in multiple connections' queues. */
typedef struct fb_message_t {
	int usecount; /**< How many message lists this message is currently used in */
	ssize_t length; /**< Length of this message */
	char *message; /**< The message */
} FB_MESSAGE;

/** Q list structure.  Per-connection list of its messages. */
typedef struct fb_messagelist_t {
    struct fb_messagelist_t *next;
	FB_MESSAGE *message;
} FB_MESSAGELIST;

/** Queue structure */
typedef struct fb_ioqueue_t {
    FB_MESSAGELIST *first;
    FB_MESSAGELIST *last;
    ssize_t consumed; /* */
} FB_IOQUEUE;

/** Connection input structure */
typedef struct fb_inputbuffer_t {
    size_t size; /**< Number of bytes currently in buffer */
    size_t capacity; /**< Maximum capacity of the buffer */
    char *message; /**< The buffer */
} FB_INPUTBUFFER;

/** Structure which collects HTTP request information as it is read. */
typedef struct http_request_t {
    bool unsupported; /**< Unsupported HTTP request (any other than HEAD or GET) */
    bool headonly; /**< flag set for HEAD request */
	char *http; /**< HTTP version from HEAD or GET request */
    char *host; /**< Requesting host.  Needed for redirects. */
	char *service_name; /**< Requested service, if service names in use per service options. */
    char *filename; /**< Request filename (Service name stripped off if used). */
	char *upgrade_type;
	char *websocket_key;
	char *websocket_protocol;
	char *websocket_version;
    char *if_modified_since; /**< Used to support caching via GET requests. */
    bool invalid; /**< flag set if invalidness was found while reading the header. */
    bool failure; /**< Web server suffering troubles. */
} FB_HTTPREQUEST;

/** Connection state information */
struct fb_connection_t {
	FB_SOCKETTYPE type; /**< Magic number so we know this is a connection. */
	FB_SERVICE *service; /**< Service to which connection belongs */
	int socket;	/**< File descriptor for this connection */
	FB_SOCKETSTATE state;	/** State: Negotiating, open, flushing, closing, etc. */
    bool greeted; /**< Did we already get a greeting in ALLOW mode? */
    bool http; /**< Flag set for HTTP connections.  Cleared when greeted. */
    bool encrypted; /**< Flag set for TLS connections. */
#ifdef WORKING_LIBGNUTLS
    gnutls_session_t tls; /**< TLS encryption state information */
#endif
    FB_HTTPREQUEST request;
    FB_IOQUEUE assembly; /**< Output to websocket, awaiting assembly to a WebSocket packet. */
    FB_IOQUEUE out; /**< Output ready to go out the socket. */
    FB_INPUTBUFFER in; /**< Input buffer */
	int domain; /**< Connection domain: PF_INET or PF_INET6 */
	union {
		struct sockaddr_in ip4addr;
#ifdef HAVE_IPV6
		struct sockaddr_in6 ip6addr;
#endif
	} origin;
	char *filename;		/**< For file connections */
	FILE *file;			/**< read() on file doesn't handle canonical stuff */
	void *context;		/**< For user use, if desired/needed */
    void *relatedObject; /**< C++ object for this connection. */
};

/* User iterators */
struct fb_iterator_t {
	FB_SERVICE *service;
	ssize_t iteration;
};

/* Message data management */
extern void fb_free_freelists (void);
extern FB_MESSAGE *fb_messagealloc(void);
extern void fb_messagefree(FB_MESSAGE *freethis);

extern FB_MESSAGELIST *fb_qalloc();
extern void fb_qfree (FB_MESSAGELIST *freethis);

extern bool fb_queue_add (FB_IOQUEUE *queue, FB_MESSAGE *message);
extern bool fb_queue_empty (FB_IOQUEUE *q);
extern void fb_queue_consume (FB_IOQUEUE *q, size_t consume);
extern void fb_queue_destroy (FB_IOQUEUE *q);

/* Destroy the service when all connections are closed. */
extern void fb_destroy_service (struct fb_service_t *service);
extern void fb_schedule_reap (FB_SERVICE *service);

/* Transfer service for HELO <svc> or http://my.server/<service> */
extern bool fb_transfer_by_name (FB_CONNECTION *connection, const char *name);

/* Register/Unregister a connection with the socket manager */
extern void fb_queue_event (FB_EVENT *event);
extern void fb_dispose_event (FB_EVENT *event);
extern bool fb_register (int socket, FB_SOCKETTYPE type, void *thing);
extern void fb_unregister (int socket);

/* Accept/Close & release a socket and its resources */
extern FB_CONNECTION *fb_accept_connection (FB_SERVICE *service, FB_SOCKETID id);
extern void fb_destroy_connection (FB_CONNECTION *connection);

/* Event handling functions */
extern bool fb_set_input_buffer_size (FB_CONNECTION *connection, size_t size);
extern bool fb_recv_input (FB_CONNECTION *connection, ssize_t byte_count);
extern FB_EVENT *fb_read_input (FB_EVENT *event, FB_CONNECTION *connection);
extern FB_EVENT *fb_new_connect (FB_EVENT *event, FB_SERVICE *service);
extern FB_EVENT *fb_send_output (FB_EVENT *event, FB_CONNECTION *connection);

/* HTTP & Websocket support */
extern FB_EVENT *fb_read_websocket_input (FB_EVENT *event, FB_CONNECTION *connection);
extern bool fb_websocket_encode (FB_CONNECTION *connection);
extern void fb_destroy_httprequest (FB_HTTPREQUEST *request);
extern void fb_collect_http_request (FB_EVENT *event, FB_HTTPREQUEST *request);
extern void fb_collect_http_parameter (char *line, FB_HTTPREQUEST *request);
extern bool fb_http_command (const char *command);
extern FB_EVENT *fb_execute_http_request (FB_EVENT *event, FB_CONNECTION *connection);

/* Utility/TLS functions */
extern const char *fb_connection_info (FB_CONNECTION *connection);
#ifdef WORKING_LIBGNUTLS
extern gnutls_certificate_client_credentials fb_get_tls_credentials (void);
extern gnutls_priority_t fb_get_tls_priorities (void);
#endif

/* Enabling/disabling events for the socket manager */
extern void fb_set_buffering (int socket_fd, bool enable);
extern void fb_set_readable (int socket, bool enable);
extern void fb_set_writable (int socket, bool enable);

/* Command line parsing */
extern int fb_create_argv (const char *commandline, char ***result, char ***remainder);
extern int fb_interpret_recurse (const FB_PARSER *parser, char *const *argv,
                                 char **argname, char **errorterm);
extern void fb_destroy_argv (char **argv);
#ifdef __cplusplus
}
#endif

#endif /* ifndef __football__service__ */
