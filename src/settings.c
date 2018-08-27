/*
 *  service.c
 *  Server-style UI for libpiano.
 *
 *  Created by Perette Barella on 2012-03-10.
 *  Copyright 2012-2014 Devious Fish. All rights reserved.
 *  Parts liberally plagiarized and adapted from PianoBar
 *  Copyright (c) 2008-2011 Lars-Dominik Braun <lars@6xq.net>
 *

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

/* application settings */

#ifndef __FreeBSD__
#define _POSIX_C_SOURCE 1 /* PATH_MAX */
#define _DEFAULT_SOURCE /* strdup() */
#define _DARWIN_C_SOURCE /* strdup() on OS X */
#endif

#include <config.h>

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <fb_public.h>
#include <ctype.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>

#include "response.h"
#include "settings.h"
#include "logging.h"
#include "support.h"
#include "users.h"

#ifndef SYSCONFDIR
#define SYSCONFDIR "/etc"
#endif

/* Adapt pianobar function in player to log to file. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
void BarUiMsg (const BarSettings_t *junk, LOG_TYPE level, char *format, ...) {
	va_list parameters;
	va_start(parameters, format);
	vflog (level, format, parameters);
	va_end(parameters);
}
#pragma GCC diagnostic pop

/* Determine if we're running as root. */
bool running_as_root (void) {
	static bool initialized = false;
	static bool root;
	if (!initialized) {
		initialized = true;
		root = (geteuid() == 0);
	}
	return root;
}

static bool have_nobody = false;
static struct passwd nobody;
static gid_t *nobody_groups;
static int nobody_groups_count;
/* When running as root, gather the user and group IDs pianod will run as. */
void select_nobody_user (const char *nobody_name, const char *group_names) {
	size_t nobody_groups_size = 0;
	assert (nobody_name);
	if (running_as_root()) {
		// Gather the primary user & group IDs.
		struct passwd *user = getpwnam (nobody_name);
		if (!user) {
			flog (LOG_ERROR, "user '%s' not found when invoking pianod as root.", nobody_name);
			flog (LOG_ERROR, "Use -n <username> to indicate user to run as.");
			exit (1);
		}
		nobody = *user;
		have_nobody = true;
		endpwent();
		
		/* Gather supplementary groups we will be running as */
		if (group_names) {
			/* Set a user-provided list of supplementary groups */
			char *groups = strdup (group_names);
			if (!groups) {
				perror ("select_nobody_user: strdup");
				exit (1);
			}
			/* Count the groups and allocate memory for the list */
			char *group = strtok (groups, ",");
			while (group) {
				nobody_groups_size++;
				group = strtok (NULL, ",");
			}
			nobody_groups = calloc (nobody_groups_size, sizeof (*nobody_groups));
			if (!nobody_groups) {
				perror ("select_nobody_user: calloc");
				exit (1);
			}
			/* Gather the group list */
			strcpy (groups, group_names);
			group = strtok (groups, ",");
			while (group) {
				struct group *group_info;
				if ((group_info = getgrnam (group))) {
					nobody_groups [nobody_groups_count++] = group_info->gr_gid;
				} else {
					flog (LOG_WARNING, "%s: %s", group, strerror (errno));
				}
				group = strtok (NULL, ",");
			}
			
		} else {
			/* Use supplementary groups from /etc/groups */
			do {
				nobody_groups_size = nobody_groups_size * 2 + 10;
				nobody_groups = realloc (nobody_groups,
										 nobody_groups_size * sizeof (*nobody_groups));
				if (!nobody_groups) {
					flog (LOG_ERROR, "select_nobody_user: realloc", strerror (errno));
					exit (1);
				}
			} while (getgrouplist(nobody_name, nobody.pw_gid, nobody_groups,
								  &nobody_groups_count) < 0);
		}
	}
}

/* Drop root privileges by setting the effective user to the real user */
void drop_root_privs (void) {
	if (running_as_root()) {
		if (setgid (nobody.pw_gid) < 0) {
			flog (LOG_ERROR, "drop_root_privs: setgid: %s", strerror (errno));
			exit (1);
		}
		if (setgroups(nobody_groups_count, nobody_groups) < 0) {
			flog (LOG_ERROR, "drop_root_privs: setgroups: %s", strerror (errno));
			exit (1);
		}
		if (setuid (nobody.pw_uid) < 0) {
			flog (LOG_ERROR, "drop_root_privs: setuid: %s", strerror (errno));
			exit (1);
		}
	}
}

/* Create/check a file and assign it to our future user ID
   so we'll be able to write to it then. */
void precreate_file (const char *filename) {
	if (running_as_root()) {
		/* select_nobody_user will only succeed if it found a nobody user, */
		/* so we can rely on that here. */
		struct stat fileinfo;
		bool exists = (stat (filename, &fileinfo) >= 0);
		bool mine = false;
		if (exists) {
			mine = fileinfo.st_uid == nobody.pw_gid;
		} else {
			// Create the file
			FILE *file = fopen (filename, "a");
			if (file) {
				fclose (file);
			} else {
				perror (filename);
				mine = false;
			}
		}
		// Reassign file ownership to the user pianod will run as
		if (!mine) {
			if (chmod (filename, S_IRUSR | S_IWUSR) < 0) {
				perror (filename);
			}
			if (chown (filename, nobody.pw_uid, nobody.pw_gid) < 0) {
				perror (filename);
			}
		}
	}
}

/*	tries to guess your config dir; somehow conforming to
 *	http://standards.freedesktop.org/basedir-spec/basedir-spec-0.6.html
 *	@param name of the config file (can contain subdirs too)
 *	@param store the whole path here
 *	@param but only up to this size
 *	@return nothing
 */
void settings_get_config_dir (const char *package, const char *filename, char *retDir,
							  size_t retDirN) {
	char *xdgConfigDir = NULL;
	
    if ((xdgConfigDir = getenv ("XDG_CONFIG_HOME")) != NULL &&
        strlen (xdgConfigDir) > 0) {
        /* special dir: $xdg_config_home */
        snprintf (retDir, retDirN, "%s/%s/%s", xdgConfigDir, package, filename);
	} else if (running_as_root()) {
		snprintf (retDir, retDirN, SYSCONFDIR "/%s.%s", package, filename);
	} else {
        if ((xdgConfigDir = getenv ("HOME")) != NULL &&
                strlen (xdgConfigDir) > 0) {
            /* standard config dir: $home/.config */
            snprintf (retDir, retDirN, "%s/.config/%s/%s", xdgConfigDir,
                    package, filename);
        } else {
            /* fallback: working dir */
            snprintf (retDir, retDirN, "%s/%s", package, filename);
        }
    }
}


/*	initialize settings structure
 *	@param settings struct
 */
void settings_initialize (BarSettings_t *settings) {
	char password_file[PATH_MAX];
	settings_get_config_dir (PACKAGE, "passwd", password_file, sizeof (password_file));

	memset (settings, 0, sizeof (*settings));
	
	/* apply defaults */
	settings->history_length = 5;
	settings->port = 4445; /* next to mserv */
    settings->http_port = settings->port + 1;
    settings->https_port = settings->http_port + 1;
	settings->volume = 0;
	settings->rpcHost = strdup (PIANO_RPC_HOST);
	settings->rpcTlsPort = NULL;
	settings->partnerUser = strdup ("android");
	settings->partnerPassword = strdup ("AC7IBG09A3DTSYM4R41UJWL07VLN8JI7");
	settings->device = strdup ("android-generic");
	settings->inkey = strdup ("R=U!LH$O2B#");
	settings->outkey = strdup ("6#26FRL$ZWD");
	settings->audioQuality = PIANO_AQ_MEDIUM;
	settings->broadcast_user_actions = true;
	settings->pause_timeout = 1800; /* Half hour */
	settings->playlist_expiration = 3600; /* One hour */
	settings->user_file = strdup (password_file);
	settings->automatic_mode = TUNE_ON_LOGINS;
	settings->pandora_retry = 60;

	memcpy (settings->tlsFingerprint,
		"\xFC\x2E\x6A\xF4\x9F\xC6\x3A\xED\xAD\x10\x78\xDC\x22\xD1\x18\x5B\x80\x9E\x75\x34",
		sizeof (settings->tlsFingerprint));
	
	/* check environment variable if proxy is not set explicitly */
	if (settings->proxy == NULL) {
		char *tmpProxy = getenv ("http_proxy");
		if (tmpProxy != NULL && strlen (tmpProxy) > 0) {
			settings->proxy = strdup (tmpProxy);
		}
	}
}



/*	free settings structure, zero it afterwards
 *	@param pointer to struct
 */
void settings_destroy (BarSettings_t *settings) {
	assert (settings);
	free (settings->rpcHost);
	free (settings->partnerUser);
	free (settings->partnerPassword);
	free (settings->device);
	free (settings->inkey);
	free (settings->outkey);
	free (settings->control_proxy);
	free (settings->proxy);
	destroy_pandora_credentials (&settings->pending);
	destroy_pandora_credentials (&settings->pandora);
	memset (settings, 0, sizeof (*settings));
}


/* Send an application setting string */
void report_setting (FB_EVENT *event, RESPONSE_CODE id, const char *setting) {
	assert (event);
	if (setting) {
		reply (event, S_DATA);
		data_reply (event, id, setting);
		reply (event, S_DATA_END);
	} else {
		data_reply (event, S_DATA_END, "Parameter is unset.");
	};
}	


/* Update an application setting string.  If newvalue is NULL, erases setting. */
bool change_setting (APPSTATE *app, FB_EVENT *event, char *newvalue, char **setting) {
	assert (app);
	assert (event);
	assert (setting);

	char *value = newvalue ? strdup (newvalue) : NULL;
	if (value || (newvalue == NULL)) {
		free (*setting);
		*setting = value;
		reply (event, S_OK);
		app->pianoparam_change_pending = true;
		return true;
	}
	data_reply (event, E_FAILURE, strerror (errno));
	reply (event, E_NAK);
	return false;
}



void report_fingerprint (FB_EVENT *event, const uint8_t *fingerprint) {
	assert (event);
	reply (event, S_DATA);
	fb_fprintf (event, "%03d %s: ", I_TLSFINGERPRINT, Response (I_TLSFINGERPRINT));
	for (int i = 0; i < TLS_FINGERPRINT_SIZE; i++) {
		fb_fprintf (event, "%02x", *((unsigned char *)fingerprint + i));
	}
	fb_fprintf (event, "\n");
	reply (event, S_DATA_END);
}


/* Change the application's TLS fingerprint. */
bool change_fingerprint (APPSTATE *app, FB_EVENT *event, char * const newvalue) {
	assert (app);
	assert (event);
	assert (newvalue);
	bool valid = (strlen (newvalue) == 2 * TLS_FINGERPRINT_SIZE);
	char *c;
	char value [3];
	long digit;
	/* Check all the digits are hex. */
	for (c = newvalue; *c; c++) {
		valid = valid && isxdigit(*c);
	}
	if (valid) {
		uint8_t *out = app->settings.tlsFingerprint;
		c = newvalue;
		value [2] = '\0';
		while (*c) {
			value [0] = *(c++);
			value [1] = *(c++);
			digit = strtol (value, NULL, 16);
			*(out++) = (uint8_t) digit;
		}
		reply (event, S_OK);
		app->pianoparam_change_pending = true;
		return true;
	}
	reply (event, E_INVALID);
	return false;
}


