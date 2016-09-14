/*
Copyright (c) 2008-2011
	Lars-Dominik Braun <lars@6xq.net>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#ifndef _SETTINGS_H
#define _SETTINGS_H

#include <piano.h>
#include <waitress.h>
#include <netinet/in.h>

#include <fb_public.h>

#include "users.h"
#include "logging.h"

typedef enum autotune_mode_t {
    /* Values must not match privilege numbers for overloaded 'which'
       parameter in send_select_users */
	TUNE_ON_LOGINS = 0x4000,
	TUNE_ON_ATTRIBUTE = 0x8000,
	TUNE_ON_ALL = 0xffff
} AUTOTUNE_MODE;

#define TLS_FINGERPRINT_SIZE (20)
typedef struct {
	/* pianobar stuff */
	char *rpcHost, *rpcTlsPort, *partnerUser, *partnerPassword, *device, *inkey, *outkey;
	int volume;
	int pandora_retry;
	CREDENTIALS pandora;
	CREDENTIALS pending;
	char *control_proxy; /* non-american listeners need this */
	char *proxy;
    char *client_location; /* Directory for HTML5 client files */
#if defined(ENABLE_CAPTURE)
	char  *capture_path;	/* Directory for stream capture */
	int capture_pathlen;
#endif
	uint8_t tlsFingerprint[TLS_FINGERPRINT_SIZE];
#if defined(USE_MBEDTLS)
    bool use_CAcerts;
    mbedtls_x509_crt ca_certs;
#endif
	PianoAudioQuality_t audioQuality;
	/* pianod */
	unsigned int history_length;
	in_port_t port;
    in_port_t http_port;
    in_port_t https_port;
	bool broadcast_user_actions;
	int pause_timeout;
	int playlist_expiration;
	char *user_file;
	AUTOTUNE_MODE automatic_mode;
	/* libao audio output settings */
	char *output_driver;
	char *output_device;
	char *output_id;
	char *output_server;
} BarSettings_t;

/* Functions dealing with dropping root privs */
extern bool running_as_root (void);
extern void select_nobody_user (const char *nobody_name, const char *nobody_groups);
extern void drop_root_privs (void);
extern void precreate_file (const char *filename);

/* Managing settings */
extern void settings_initialize (BarSettings_t *);
extern void settings_set_defaults (BarSettings_t *);
extern void settings_destroy (BarSettings_t *);
extern void settings_get_config_dir (const char *, const char *, char *, size_t);

/* pianobar adapter.  Don't use this.  Here because this file overlaps w/ player.c,
   and this avoids having to patch that file every time it updates. */
extern void BarUiMsg (const BarSettings_t *, LOG_TYPE level, char *format, ...);
#define MSG_ERR (LOG_ERROR)

#endif /* _SETTINGS_H */
