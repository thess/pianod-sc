///
/// Football utility functions.
/// @file       fb_utility.c - Football socket abstraction layer
/// @author     Perette Barella
/// @date       2014-03-26
/// @copyright  Copyright 2012–2014 Devious Fish. All rights reserved.
///

#include <config.h>

#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include <memory.h>
#include <time.h>
#include <arpa/inet.h>

#include "fb_public.h"
#include "fb_service.h" // Needs to precede gnutls to determine workability

#ifdef WORKING_LIBGNUTLS
#include <gnutls/gnutls.h>
#endif


static int logmode = 0;

/** Set logging level.
    Set which messages will be collected by the internal logging function, or
    provide an alternate logging function.  When using a private logging function,
    logtype is not used.
    @param logtype a bitmask indicating which data to log.
    @param func a logging function that replaces the default one.
 */
void fb_set_logging (int logtype, FB_LOGGING_FUNCTION func) {
	logmode = logtype;
    if (func != NULL) {
        fb_log = func;
    }
}

/** @internal
    Default logging implementation: Log stuff to stderr.
    The file, line, and func parameters are removed for release builds
    (when NDEBUG is set.)
    An application can provide an alternative function using the second
    parameter to fb_set_logging.
    @see fb_set_logging
    @see FB_WHERE
    @param file the filename from which the message originates.
    @param line the line number at which the message originates.
    @param func the function from which the message originates.
    @param level a bitmask indicating the type of the message (0=error, always logged)
    @param format a printf-style format string, with parameters to follow. */
void fb_log_impl (
#ifndef NDEBUG
                  const char *file, int line, const char *func,
#endif
                  int level, const char *format, ...) {
	char date [22];
	if (level == 0 || (logmode & level)) {
        va_list parameters;
        va_start (parameters, format);
		time_t now = time (NULL);
		strftime(date, sizeof (date),"%Y-%m-%d %H:%M:%S", localtime(&now));
#ifdef NDEBUG
        fprintf (stderr, "%s: ", date);
#else
        const char *shortfile = strrchr (file, '/');
		fprintf (stderr, "%s: %s:%d (%s): ", date, shortfile ? shortfile + 1 : file, line, func);
#endif
		vfprintf(stderr, format, parameters);
        fputc('\n', stderr);
        va_end(parameters);
	}
}
FB_LOGGING_FUNCTION fb_log = fb_log_impl;


/** @internal
    Return a a connection's opposing end as a string.
    @param connection the connection to get the string of.
    @return a string with the info, or a stand-in value. */
const char *fb_connection_info (FB_CONNECTION *connection) {
    static char buffer [200];
    if (connection->file) {
        return "a file";
    }
#ifdef HAVE_IPV6
    const char *address = inet_ntop (connection->domain,
                                     connection->domain == PF_INET6 ?
                                     (struct in_addr *) &(connection->origin.ip6addr.sin6_addr) :
                                     &(connection->origin.ip4addr.sin_addr),
                                     buffer, sizeof (buffer));
#else
    const char *address = inet_ntop (connection->domain,
                                     &(connection->origin.ip4addr.sin_addr),
                                     buffer, sizeof (buffer));
#endif
    if (address) {
        size_t len = strlen (address);
        if (len < (sizeof (buffer) - 50)) {
#ifdef HAVE_IPV6
            sprintf (buffer + len, " port %d",
                     connection->domain == PF_INET6 ?
                     ntohs (connection->origin.ip6addr.sin6_port) :
                     ntohs (connection->origin.ip4addr.sin_port));
#else
            sprintf (buffer + len, " port %d",
                     ntohs (connection->origin.ip4addr.sin_port));
#endif
        }
    }
    return address ? address : "???";
}


/** calloc() with ability to expand the allocated data.
    Does nothing if the actual capacity meets or exceeds the requested capacity.
    When expansion occurs, the added elements are zeroed out.
    @param data the allocation to expand.  May be *NULL for initial allocation.
    @param capacity the capacity of the allocation, which may be adjusted by this call.
    @param newcapacity the desired capacity of the allocation.
    @param itemsize the size of data items in the allocation.
    @return true on success, false on failure (existing allocation remains valid). */
bool fb_expandcalloc (void **data, size_t *capacity, size_t newcapacity, size_t itemsize) {
    assert ((capacity != 0) == (data != NULL));
    if (newcapacity <= *capacity) {
        /* It's big enough already */
        return true;
    }
    /* Expand in reasonable chunks when necessary, not by drips. */
    size_t mincapacity = *capacity * 2 + 25;
    if (mincapacity > newcapacity) {
        newcapacity = mincapacity;
    }
    size_t oldsize = *capacity * itemsize;
    size_t newsize = newcapacity * itemsize;
    char *newdata = realloc (*data, newsize);
    if (!newdata) {
        return false;
    }
    *data = (void *) newdata;
    memset(newdata + oldsize, 0, newsize - oldsize);
    *capacity = newcapacity;
    return true;
}


/* GNUTLS initialization code cookbooked from GNUTLS manual, pp. 189-192 */
#ifdef WORKING_LIBGNUTLS

#define KEY_FILE            "x509-server-key.pem"
#define CERTIFICATE_FILE    "x509-server.pem"

static bool fb_tls_initialized;
static gnutls_certificate_credentials_t fb_tls_credentials;
static gnutls_dh_params_t fb_tls_dh_params;
static gnutls_priority_t fb_tls_priorities;

/** @internal
    Get TLS credentials.
    @return TLS credentials. */
gnutls_certificate_client_credentials fb_get_tls_credentials () {
    assert (fb_tls_initialized);
    return fb_tls_credentials;
}

/** @internal
    Get TLS priorities.
    @return TLS priorities. */
gnutls_priority_t fb_get_tls_priorities () {
    assert (fb_tls_initialized);
    return fb_tls_priorities;

}

/** Initialize TLS support structures.
    This may be called harmlessly when built without TLS support.
    @param path location/prefix of the key/certificate data files.
           path MUST have a '/' if the name is a directory
             - "/etc/" becomes "/etc/x509-server-key.pem".
             - "/etc" becomes "/etcx509-server-key.pem".
             - "/etc/myapp-" becomes "/etc/myapp-x509-server-key.pem".
    @return true on success, false on failure (allocation error, files not found).
 */
bool fb_init_tls_support (const char *path) {
    assert (path);
    assert (!fb_tls_initialized);

    int status = gnutls_certificate_allocate_credentials(&fb_tls_credentials);
    if (status != GNUTLS_E_SUCCESS) {
        return false;
    }

    char *key_file, *cert_file;
    if ((asprintf (&key_file, "%s%s", path, KEY_FILE) < 0)) {
        fb_perror("asprintf");
        key_file = NULL;
    }
    if ((asprintf (&cert_file, "%s%s", path, CERTIFICATE_FILE) < 0)) {
        fb_perror("asprintf");
        cert_file = NULL;
    }

    if (key_file && cert_file) {
        status = gnutls_certificate_set_x509_key_file (fb_tls_credentials, cert_file, key_file, GNUTLS_X509_FMT_PEM);
        if (status == GNUTLS_E_SUCCESS) {
            // Initialize Diffie-Hellman things; see p. 190.
#ifdef HAVE_GNUTLS_SEC_PARAM_TO_PK_BITS
            unsigned int bits = gnutls_sec_param_to_pk_bits(GNUTLS_PK_DH, GNUTLS_SEC_PARAM_LEGACY);
#else
            unsigned int bits = 1776; /* It's what the function returns on my system */
#endif
            if ((status = gnutls_dh_params_init (&fb_tls_dh_params)) != GNUTLS_E_SUCCESS) {
                fb_log (FB_WHERE (FB_LOG_ERROR), "gnutls_dh_params_init: %s", gnutls_strerror (status));
            } else if ((status = gnutls_dh_params_generate2 (fb_tls_dh_params, bits)) != GNUTLS_E_SUCCESS) {
                fb_log (FB_WHERE (FB_LOG_ERROR), "gnutls_dh_params_generate2: %s", gnutls_strerror (status));
            } else {
                gnutls_certificate_set_dh_params (fb_tls_credentials, fb_tls_dh_params);
                status = gnutls_priority_init (&fb_tls_priorities,
                                                "PERFORMANCE:%SERVER_PRECEDENCE", NULL);
                if (status == GNUTLS_E_SUCCESS) {
                    free (key_file);
                    free (cert_file);
                    fb_tls_initialized = true;
                    return true;
                } else {
                    fb_log (FB_WHERE (FB_LOG_ERROR), "gnutls_priority_init: %s\n", gnutls_strerror (status));
                }
            }
        } else {
            if (status != GNUTLS_E_SUCCESS) {
                fb_log (FB_WHERE (FB_LOG_ERROR), "%s / %s: gnutls_certificate_set_x509_key_file: %s",
                         cert_file, key_file, gnutls_strerror (status));
            }
        }
    } else {
        fb_perror ("asprintf");
    }
    free (key_file);
    free (cert_file);
    gnutls_certificate_free_credentials (fb_tls_credentials);
    memset (&fb_tls_credentials, 0, sizeof (fb_tls_credentials));
    return false;
}
#else
bool fb_init_tls_support (const char *path) {
    assert (path);
    fb_log (FB_WHERE (FB_LOG_WARNING), "Invoked on build without WORKING_LIBGNUTLS (mostly harmless).");
    return false;
}
#endif
