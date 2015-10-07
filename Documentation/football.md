# Football
Football is a socket abstraction layer for building line-oriented socket services in the style of FTP, POP3, SMTP, etc.  It serves both IPv4 and IPv6, with TLS available for both (requires GNUTLS library).  Football also allows files to be treated as a connection.  Football is not multithreaded but does allow multiple services and multiple connections on each service.

Football also features Websocket support and a rudimentary HTTP server.  These connections use a separate port, and HTTP connections to the line socket will fail.  However, line connections to the HTTP port will work if greetings are enabled and the client greets correctly.  Secure line sessions may be established by connecting to the HTTPS port, TLS handshaking, then issuing the line greeting.

If provided with a directory to serve, football will serve files in response to HTTP and HTTPS requests.  No server-side processing or file compression is performed.

Websockets are an alternate to the line protocol.  A line of text in line-oriented mode (either command or response) is equivalent to one Websocket packet with the newline removed.  The type of connection is abstracted from the application code.

Football includes a primitive web server with enough function to serve a web applet to clients.

## Football vs. REST
As REST services are becoming ubiquitous and ever-easier to implement, wouldn't it make more sense to use REST?

In many scenarios, yes.  You should probably use REST if an application is:

1.	Web only.
2.	Transactional—that is, it generates a request and expects a response, without need for spontaneous notifications.

However, scenarios such as a chat application where communication ideally happens both ways in real time do not work well with REST or traditional http.  Websockets are an attempt to solve this, but provide only a rudimentary packet-exchange system, not a transaction framework.

Line-oriented Football was in place when the Websockets RFC was complete.  Translating lines to Websocket packets was an obvious extension, and initially done with separate software (wsgw, the Websocket gateway), allowing reuse of the application protocol and processing mechanism over the new transport layer.

On the simplicity of line-oriented protocols: Many older Internet protocols possess similar line-oriented mechanisms.  A friend once commented, "Every few years, people rediscover the power of the text file."  There is ease of understanding and the ability to read, explore, experiment and troubleshoot without special tools.  Line-oriented protocols have a similar simplicity, and while there are limitations to what they can carry, a little cleverness goes a long way.  Simplicity avoids the headaches of complicated encoding that make speaking a protocol difficult.  Football harnesses the power of the text protocol.

## Overview
The general use of football is:

1.	Initialize (if using TLS)
2.	Create a service
3.	Wait for events.
4.	Service events.
5.	If not shutting down, Goto 3.
6.	Destroy service.

## Initializing TLS
TLS support requires GNU TLS.  Your application must first call `gnutls_global_init()` to initialize the library.

Next you must call `fb_init_tls_support(char *path)` to provide the location of X509 certificates and key files.  If `path` is a directory, it must include a trailing slash; if not, the last directory is treated as a prefix for the filenames:

* `/etc/pianod-` becomes `/etc/pianod-x509-server-key.pem`.
* `/etc/pianod/` becomes `/etc/pianod/x509-server-key.pem`.
* `/etc` becomes `/etcx509-server-key.pem`.

The TLS files Football requires are:

* `x509-server-key.pem`
* `x509-server.pem`

See the [GNUTLS certtool documentation] for instructions on creating a certificate.

Lastly, set the `https_port` field in the service options when creating services.

[GNUTLS certtool documentation]: http://www.gnutls.org/manual/html_node/certtool-Invocation.html

## Creating a service

	struct fb_service_t *fb_create_service (FB_OPTIONS *options)

Creates a service on port(s) specified in the options.  For either type of port, Football attempts to create both a IPv4 and IPv6 socket.  Creation is successful if any of the 4 possible ports are created.  Options include:

* `line_port`: The line-oriented port, or 0 to disable.
* `http_port`: The HTTP port, or 0 to disable.
* `https_port`: The HTTP secure port, or 0 to disable.
* `greeting_mode`: controls line-oriented greetings, which allow both line and Websocket traffic to share a port by accepting a "greeting" instead of the usual GET on the HTTP port.
	* `FB_GREETING_OFF`: The line port session starts on connection, and does not use greetings. Greeting the HTTP port is a protocol error.
	* `FB_GREETING_REQUIRED`: The line port waits for a required greeting before starting the session.  Greeting the HTTP port triggers a line session.
	* `FB_GREETING_FALLBACK`: Like `REQUIRED`, but invalid input on the either port initiates a line session.
	* `FB_GREETING_ALLOW`: The line port session starts on connection, but greetings are filtered out of the line protocol. Greeting the HTTP port triggers a line session, other invalid input is a protocol error.
* `greeting`: The greeting text.  Default is "HELO".  To accommodate future enhancements allowing port sharing, it is suggested clients greet with with a requested service name: `HELO pianod`.
* `name`: The name of the service, presently only required in URLs if set.  Future enhancements to Football could allow multiple services to share a port, using 'name' in greeting and URLS to disambiguate.
* `queue_size`: the new connection queue size.  Football regularly accepts new connections, so unless your application is expected to poll infrequently, a small number is fine.
* `context_size`: the size of a per-connection context created for each connection.  Context is created acceptance and destroyed at closure; it is entirely for use by your application.

## The event model
After initializing and setting up a service, an application will enter a run loop.

	FB_EVENT *fb_wait ();	/* Wait indefinitely */
	FB_EVENT *fb_poll ();	/* Check without waiting */
	FB_EVENT *fb_poll_until (time_t untilwhen); /* Wait until this time */
	FB_EVENT *fb_poll_with_timeout (double timeout); /* Amount of time */

Most applications will simply call one of these, depending on the nature of the application, but it is possible to mix the calls as needed.  These functions *should always* return an event (a timeout being an event too); a NULL response indicates a Football failure.

`FB_EVENT` is a structure exposing:

* `type`—the type of event
* `socket`—To identify your own sockets registered with Football
* `context`—A pointer to the connections context, if you requested
* `command`—the statement received
* `argc`—the number of terms in the statement received
* `argv`—the statement parsed into an argv-style array
* `connection`—a Football connection handle
* `service`—the service that owns the connection

Other things in `FB_EVENT` are private.

## Event types
### New connections
A new connection creates an `FB_EVENT_CONNECT` event.  Application code can rely on:

* `type`
* `context`, which is initialized with all zero bytes.
* `service` and `connection`

It is safe to use the connection at this point.

To close the connection at some later time, use:

	fb_close_connection (struct fb_connection_t *connection)

Note you should avoid closing connections twice.  In debug mode, Football will assert() if you do.  However, the possibility of a race condition exists if a connection is dropped at the other end.  Football will handle this correctly when debugging is disabled (NDEBUG is defined).

### Command received
When a command has been received, Football parses it and returns `FB_EVENT_INPUT` with the command in both raw and argv-arranged forms.  Application code can rely on:

* `type`
* `context`
* `service` and `connection`
* `command`, `argv`, and `argc`

### Connection closure
`FB_EVENT_CLOSE` occurs when the connection is closing.  It is still safe to write to the connection—however, there is no guarantee the connection is still open, as it may have have closed from the other end.  Writes to the connection in response to `FB_EVENT_CLOSE` are not guaranteed to complete.  The event includes:

* `type`
* `context`
* `service` and `connection`

### Timeouts
`FB_EVENT_TIMEOUT` occurs with the Football polling calls except for `fb_wait`.  The event includes only the type.


### Service Termination
When service closure is requested, new connections are no longer accepted, all open connections will be closed and flushed, and finally a FB_EVENT_STOPPED event delivered.  The event includes:

* `type`
* `service`

## User socket events

Documentation forthcoming.


## Writing to a connection
Football offers both directed and broadcast messaging.  Writing to a connection guarantees that Football will deliver it, so long as the other end does not close the connection and `FB_EVENT_CLOSE` has not yet been delivered for that connection.  However, Football offers no facilities for verifying deliver; if the other end closes the connection or the network goes down, pending writes are silently dropped.

For simplicity, Football uses `printf`(3)-style formatting, and the write functions are named accordingly:

	int fb_fprintf (void *thing, const char *format, ...);
	int fb_vfprintf (void *thing, const char *format, va_list parameters);
	int fb_bfprintf (void *thing, const char *format, ...);
	int fb_bvfprintf (void *thing, const char *format, va_list parameters);

`fb_fprintf` and `fb_vfprintf` correspond to the non-Football equivalents of the standard library `fprintf` and `vfprintf`, but accept the polymorphic `thing` (either a service, connection, or event) instead of a stream.  If `thing` is a service, the message is broadcast to all open connections on that service; if `thing` is a connection or event, the message is directed.

The "b" variations broadcast to a service, even if a connection or event is specified.


## Iterating Connections
To access all the connections on a service, create an iterator using the service returned from `fb_create_service()`.

	struct fb_iterator_t * fb_new_iterator (struct fb_service_t *);

The iterator is then used to walk through the open connections:

	FB_EVENT *fb_iterate_next (struct fb_iterator_t *);

The event includes:

* `type`, which will be either `FB_EVENT_ITERATOR` if the connection is open, or `FB_EVENT_ITERATOR_CLOSE` if closure has been initiated.
* `context`
* `service` and `connection`

On completion, you must release resources allocated by the iterator:

	void fb_destroy_iterator (fb_iterator_t *);

For example:

	struct fb_iterator_t *it = fb_new_iterator (service);
	if (it) {
		FB_EVENT *event;
		while (event = fb_iterate_next (it)) {
			/* Do something with this connection */
		}
		fb_destroy_iterator (it);
	}


## Command Parser
Parsing and validating command lines is a hassle.  Football returns commands broken into argv-style arrays in `FB_EVENT` to ease this, but also provides a generic parser to do more of the heavy lifting for you.  The parser ignores case.

To use the parser, you create a parser instance, then load statement definitions into it.  The statements resemble the synopsis formats used in manual pages and usage messages.  You also give each statement a positive number as an ID, which may or may not be unique depending on your needs.  In this phase, football builds a parse tree from all your statements and does some optimization.  If your statements are invalid or conflict, football warns about it and fails in this phase.

Then, when you're ready to parse, you call football with the parser instance and your argv-style array, and get back an error code (negative value) or the matching statement ID.  Now you can use a big `switch`/`case` statement to decide what to do; you can often access parameters via argv with fixed indices if you set up your definitions right.  

It is possible to create multiple parsers either for different command sets, or to break up complex definitions.

### Configuring the parser
A parser definition might look like this, though usually much longer:

	typedef enum my_commands_t {
		NOP, HELP, STATUS, HISTORY, CREATEUSER,
		ADDTOGROUP, DELETEUSER
	} Command_t; // Use positive numbers; negatives are reserved for errors.
	static FB_PARSE_DEFINITION statements[] = {
	    { NOP,        "" },	                /* Null input is no op. */
	    { NOP,        "# ..." },            /* Comment */
	    { HELP,       "help [{command}]" }, /* Request help */
	    { HELP,       "? [{command}]" },
	    { STATUS,     "status" },           /* Request status */
	    { HISTORY,    "history [{index}]" },/* Request history */
	    { CREATEUSER, "create <guest|user|admin> {user} {passwd}" },
	    { ADDTOGROUP, "add user {user} to [group] {group}" },
	    { DELETEUSER, "delete user {user} ... }" },
	};

Statement formats can be composed of:

* `keyword` matches on that word in that position (case is ignored).
* `{value}` accepts any value in that position
* `<one|two>` accepts any of the listed words in that position
* `[optional]` accepts an optional keyword
* `[three|four]` optionally accepts any single keyword
* `[{optional-value}]` accepts an optional value, only as the final word
* ... allows 0 or more additional parameters

Note that the `[optional]` construct is troublesome: it makes the subsequent `{value}` field positions less determinate, which makes much of the effort worthless.  If you avoid this construct, you can typically just grab the values you want out of an event's argv.  This warning does not apply to the `[{optional-value}]` construct, which is handy; you can determine if it was given by whether argv[index] is null.

Values may have a type and range as follows:
* `{string}`
* `{#numeric}` -- accepts a leading minus and digits, no decimal.
* `{#numeric:3-5}` -- accepts a decimal integer in the range
* `{#numeric:3.0-5.0}` -- Accepts a decimal value in the range
`{#numeric:0x3-0x5}` -- Accepts octal, decimal, or hexadecimal values in the range

### Using the parser
Creating a parser goes like this:

	/* Create a service */
	FB_PARSER *my_parser;
	if (my_parser = fb_create_parser()) {
		if (fb_parser_add_statements (my_parser, statements)) {
			/* Parser is ready to go */
		} else {
			/* Fail: Couldn't initialize parser */
		}
	} else {
		/* Fail: Couldn't create parser. */
	}

At some later point in your application, when you want something parsed:

	char *errorpoint;
	Command_t command;
	command = fb_interpret (my_parser, event->argv, &errorpoint);
	if (command < 0) {
		/* Parse error */
	} else {
		/* Do your thing */
	}

`fb_interpret` returns the negative values for errors, and sets `errorpoint` to the offending term.

* `FB_PARSE_FAILURE`: The parser crapped out.
* `FB_PARSE_INCOMPLETE`: The input matches but is not complete.  `errorpoint` indicates the last term.
* `FB_PARSE_INVALID_KEYWORD`: One of the terms does not match any statements.
* `FB_PARSE_EXTRA_TERMS`: The input matches, but there are extra terms following the input.  `errorpoint` indicates the first extra term.
* `FB_PARSE_NUMERIC`: An input term was supposed to be numeric but is not.
* `FB_PARSE_RANGE`: The input term is a valid number but outside the required numeric range.

Additional values may be added in the future; be sure to treat all negative values as parse errors.

Since a parser typically stays around for the duration, you probably don't need to free it.  But if you do, or you just want to be good about freeing things:

	fb_parser_destroy (my_parser);

## Logging
Football's internal logger sends "real" errors (failure within Football) to standard error.  This does not include "expected" errors such as a connection terminating unexpectedly.

Other events use categories.  Each event has 1 or more categories associated with it.  By default, only "real" errors are logged; additional categories can be logged with:

	void fb_set_logging (category_bitmask, log_function)

The `category_bitmask` indicates the categories to log.  Multiply-categorized events are logged if _any_ category matches.  See `fb_service.h` for current bitmask values.  Note connection, TLS and HTTP error categories log connection failures such as unexpected close, bad TLS handshake, or invalid HTTP request.

The `log_function` parameter is normally NULL, but if Football's internal log function does not meet your needs, you can supply your own function here.  The function prototype varies depending on whether `NDEBUG` is set (as used by `assert(3)`).

Without NDEBUG:
	log_function (char *file, int *line, char *function,
	              int category, char *format, ...)

With NDEBUG:
	log_function (int category, char *format, ...)

When using a custom log function, all events are passed to it regardless of the categories set by `fb_set_logging`.  `format` is printf-style, with parameters to follow.