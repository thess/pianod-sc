/*
 *  service.c
 *  Server-style UI for libpiano.
 *
 *  Created by Perette Barella on 2012-03-10.
 *  Copyright 2012-2014 Devious Fish. All rights reserved.
 *  BarUiPianoCall and BarPianoHttpRequest plagiarized from PianoBar
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

#ifndef __FreeBSD__
#define _POSIX_C_SOURCE 1 /* fileno() */
#define _DEFAULT_SOURCE /* strdup() */
#define _DARWIN_C_SOURCE /* strdup() on OS X */
#endif

#include <config.h>

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <strings.h>
#include <assert.h>
#include <ctype.h>
#include <pthread.h>
#include <ao/ao.h>
#include <math.h>

#include <sys/types.h>

#ifdef HAVE_RES_INIT
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#endif

#include <piano.h>

#include "support.h"
#include "pianod.h"
#include "event.h"
#include "logging.h"
#include "response.h"
#include "pianoextra.h"


/* ---------- Start of pianobar plagiarized stuff ---------- */

/*	fetch http resource (post request)
 *	@param waitress handle
 *	@param piano request (initialized by PianoRequest())
 */
static WaitressReturn_t BarPianoHttpRequest (WaitressHandle_t *waith,
		PianoRequest_t *req) {
	waith->extraHeaders = "Content-Type: text/xml\r\n";
	waith->postData = req->postData;
	waith->method = WAITRESS_METHOD_POST;
	waith->url.path = req->urlPath;
	waith->url.tls = req->secure;

	return WaitressFetchBuf (waith, &req->responseData);
}

/*	piano wrapper: prepare/execute http request and pass result back to
 *	libpiano (updates data structures)
 *	@param app handle
 *	@param request type
 *	@param request data
 *	@param stores piano return code
 *	@param stores waitress return code
 *	@return 1 on success, 0 otherwise
 */
int BarUiPianoCall (APPSTATE * const app, PianoRequestType_t type,
		void *data, PianoReturn_t *pRet, WaitressReturn_t *wRet) {
	PianoRequest_t req;

	memset (&req, 0, sizeof (req));

	/* repeat as long as there are http requests to do */
	do {
		req.data = data;

		*pRet = PianoRequest (&app->ph, &req, type);
		if (*pRet != PIANO_RET_OK) {
			send_response_code(app->service, E_FAILURE, PianoErrorToStr (*pRet));
			PianoDestroyRequest (&req);
			return 0;
		}

		*wRet = BarPianoHttpRequest (&app->waith, &req);
		if (*wRet != WAITRESS_RET_OK) {
			send_response_code(app->service, E_NETWORK_FAILURE, WaitressErrorToStr (*wRet));
			if (req.responseData != NULL) {
				free (req.responseData);
			}
			PianoDestroyRequest (&req);
			return 0;
		}

		*pRet = PianoResponse (&app->ph, &req);
		if (*pRet != PIANO_RET_CONTINUE_REQUEST) {
			/* checking for request type avoids infinite loops */
			if (*pRet == PIANO_RET_P_INVALID_AUTH_TOKEN &&
					type != PIANO_REQUEST_LOGIN) {
				/* reauthenticate */
				PianoReturn_t authpRet;
				WaitressReturn_t authwRet;
				PianoRequestDataLogin_t reqData;
				reqData.user = app->settings.pandora.username;
				reqData.password = app->settings.pandora.password;
				reqData.step = 0;

				flog (LOG_GENERAL, "Reauthenticating with server...");
				if (!BarUiPianoCall (app, PIANO_REQUEST_LOGIN, &reqData, &authpRet,
						&authwRet)) {
					*pRet = authpRet;
					*wRet = authwRet;
					if (req.responseData != NULL) {
						free (req.responseData);
					}
					PianoDestroyRequest (&req);
					return 0;
				} else {
					/* try again */
					*pRet = PIANO_RET_CONTINUE_REQUEST;
					flog (LOG_GENERAL, "Reauthenticating reauthentication...");
				}
			} else if (*pRet != PIANO_RET_OK) {
				send_data (app->service, E_AUTHENTICATION, PianoErrorToStr (*pRet));
				if (req.responseData != NULL) {
					free (req.responseData);
				}
				PianoDestroyRequest (&req);
				return 0;
			} else {
				flog (LOG_GENERAL, "Server authentication ok");
			}
		}
		/* we can destroy the request at this point, even when this call needs
		 * more than one http request. persistent data (step counter, e.g.) is
		 * stored in req.data */
		if (req.responseData != NULL) {
			free (req.responseData);
		}
		PianoDestroyRequest (&req);
	} while (*pRet == PIANO_RET_CONTINUE_REQUEST);

	return 1;
}


/* pianod wrapper for PianoBar's BarUIPianoCall. */
bool piano_transaction (APPSTATE *app, FB_EVENT *event, PianoRequestType_t type, void *data) {
	PianoReturn_t pRet;
	WaitressReturn_t wRet;

	assert (app);
	assert (type);

	bool result = BarUiPianoCall (app, type, data, &pRet, &wRet);
	if (event) {
		if (pRet != PIANO_RET_OK) {
			data_reply (event, E_NAK, PianoErrorToStr (pRet));
		} else if (wRet != WAITRESS_RET_OK) {
			data_reply (event, E_NAK, WaitressErrorToStr (wRet));
		} else {
			reply(event, S_OK);
		}
	}
	return result;
}


/*	prepend song to history, must not be a list of songs as ->next is modified!
 */
void prepend_history (APPSTATE *app, PianoSong_t *song) {
	if (app->settings.history_length != 0) {
		PianoSong_t *tmpSong;

		app->song_history = PianoListPrependP (app->song_history, song);

		/* limit history's length */
		/* start with 1, so we're stopping at n-1 and have the
		 * chance to set ->next = NULL */
		unsigned int i = 1;
		tmpSong = app->song_history;
		while (i < app->settings.history_length && tmpSong != NULL) {
			tmpSong = PianoListNextP (tmpSong);
			++i;
		}
		/* if too many songs in history... */
		if (tmpSong != NULL) {
			PianoSong_t *delSong = PianoListNextP (tmpSong);
			tmpSong->head.next = NULL;
			PianoDestroyPlaylist (delSong);
		}
	}
}

/* ---------- End of plagiarized stuff ---------- */


/* To minimize playlist churn, delete songs on the front of the queue
   that aren't among the selected station(s) anymore but keep the rest.
   This is intentionally lazy; if someone pops out to get the laundry,
   they might be back while this playlist is still "in motion". */
void purge_unselected_songs (APPSTATE *app) {
	PianoSong_t *song;
	while (((song = app->playlist))) {
		if (app->selected_station == NULL) {
			/* Stop has been requested.  Dump queue */
		} else if (app->selected_station->isQuickMix) {
			/* See if the track's station is among the quickmix stations */
			PianoStation_t *station = PianoFindStationById(app->ph.stations, song->stationId);
			if (station) {
				assert (!station->isQuickMix);
				if (station->useQuickMix) {
					return;
				}
			} else {
				/* Can occur if station was deleted or Pandora credentials changed */
				flog (LOG_WARNING, "purge_unselected_songs: Station id# %s not found", song->stationId);
			}
		} else {
			/* See if the current station is the station of the track */
			if (strcmp (song->stationId, app->selected_station->id) == 0) {
				return;
			}
		}
		/* Remove the song from the playlist */
		app->playlist = PianoListNextP (song);
		song->head.next = NULL;
		PianoDestroyPlaylist (song);
		/* If we removed the last song, reset the station cache
		   so we don't churn getting unusable playlists. */
		if (!app->playlist) {
			app->update_station_list = 0;
		}
	}
}


/* Compare two station lists and determine what (if anything) has changed. */
static bool check_for_station_changes (APPSTATE *app, PianoStation_t *old_stations, PianoStation_t *new_stations) {
	bool quick_mix_changed = false;
	bool station_added = false;
	bool station_removed = false;
	PianoStation_t *past, *present;

	/* Check for stations removed. */
	past = old_stations;
	PianoListForeachP (past) {
		if ((present = PianoFindStationById(new_stations, past->id))) {
			quick_mix_changed = quick_mix_changed || (present->useQuickMix != past->useQuickMix);
		} else {
			station_removed = true;
			if (past->useQuickMix) quick_mix_changed = true;
			flog (LOG_GENERAL, "check_for_station_changes: Drop Station %s", past->name);
		}
	}
	/* Check for stations added. */
	present = new_stations;
	PianoListForeachP (present) {
		if ((past = PianoFindStationById(old_stations, present->id))) {
			quick_mix_changed = quick_mix_changed || (present->useQuickMix != past->useQuickMix);
		} else {
			station_added = true;
			if (present->useQuickMix) quick_mix_changed = true;
			flog (LOG_GENERAL, "check_for_station_changes: Add Station %s", present->name);
		}
	}
	if (station_added || station_removed) {
		send_response (app->service, I_STATIONS_CHANGED);
	}
	if (quick_mix_changed) {
		flog (LOG_GENERAL, "check_for_station_changes: Quick mix has been changed");
		send_response (app->service, I_MIX_CHANGED);
	}
	return (quick_mix_changed || station_added || station_removed);
}


/* Get initial or refresh station list.
 * Refreshing the station list:
 * - Updates the QuickMix and station list to reflect changes made from another client.
 * - Handles the active Pandora account being switched out.
 */
bool update_station_list (APPSTATE *app) {
	assert (app);
	bool ret;

	if (app->update_station_list >= time (NULL)) {
		/* Use existing list */
		return (app->ph.stations != NULL);
	}

	/* libpiano assumes the station list doesn't change except under its control. */
	/* We have to play around to check for this.  First, stash the old station list */
	PianoStation_t *oldStations = app->ph.stations;
	app->ph.stations = NULL;

	/* Retrieve the new list. */
	/* On success, keep the new list; on failure, use the old list. */
	flog (LOG_GENERAL, "Retrieving/updating station list");
	if ((ret = piano_transaction (app, NULL, PIANO_REQUEST_GET_STATIONS, NULL))) {
		/* Announce any changes */
		check_for_station_changes (app, oldStations, app->ph.stations);
		/* Update the current station to use the same station but in the new list */
		if (app->selected_station) {
			app->selected_station = PianoFindStationById (app->ph.stations, app->selected_station->id);
			if (!app->selected_station) {
				send_response_code (app->service, E_RESOURCE, "Selected station has been deleted.");
				send_selectedstation (app->service, app);
			}
		}
		PianoDestroyStations(oldStations);
	} else {
		/* Restore the original station list */
		PianoDestroyStations(app->ph.stations);
		app->ph.stations = oldStations;
	}
	/* Buffer stations for 5 minutes if we have a list,
	 1 minute if waiting for a list so we don't churn too fast. */
	app->update_station_list = time (NULL) + (app->ph.stations ? 300 : 60);
	return ret;
}


/* Authenticate user and switch manage credentials.
 * In the event of network problems, retry the new credentials again in a bit.
 * If the new credentials are bad, throw them away.
 * If the new credentials are good, start using them.
 * Note: 'event' may be an event (as when called interactively) or NULL
 * (as when called during a periodic retry).
 */
void set_pandora_user (APPSTATE *app, FB_EVENT *event) {
	PianoReturn_t pRet;
	WaitressReturn_t wRet;
	PianoRequestDataLogin_t reqData;

	assert (app->settings.pending.username);
	assert (app->settings.pending.password);
	reqData.user = app->settings.pending.username;
	reqData.password = app->settings.pending.password;
	reqData.step = 0;

	app->retry_login_time = 0;

	/* Some systems cache the DNS configuration in a global/static variable. */
	/* If pianod is started at boot, DNS might not be up, these systems get */
	/* stuck bad data gets cached. Call the init routine manually to get updates. */
#ifdef HAVE_RES_INIT
	res_init();
#endif

    bool changed = !app->settings.pandora.username ||
        strcmp (app->settings.pandora.username, app->settings.pending.username) != 0 ||
        strcmp (app->settings.pandora.password, app->settings.pending.password) != 0;
	if (changed) {
        send_status(app->service, "Logging in to server");
    }
	if (!changed || BarUiPianoCall (app, PIANO_REQUEST_LOGIN, &reqData, &pRet, &wRet)) {
		if (event) {
			reply (event, S_OK);
		}
	} else if (wRet != WAITRESS_RET_OK) {
		if (event) {
			data_reply (event, E_REQUESTPENDING, WaitressErrorToStr (wRet));
		}
		/* Transient error; retry login again later. */
		app->retry_login_time = time(NULL) + app->settings.pandora_retry;
		return;
	} else if (pRet == PIANO_RET_INVALID_LOGIN) {
		/* Throw away the bad credentials. */
		destroy_pandora_credentials (&app->settings.pending);
		if (event) {
			data_reply (event, E_CREDENTIALS, PianoErrorToStr (pRet));
		} else {
			send_response_code (app->service, E_AUTHENTICATION, PianoErrorToStr (pRet));
		}
		event_occurred (app->service, EVENT_AUTHENTICATED, E_CREDENTIALS);
		return;
	} else if (pRet != PIANO_RET_OK) {
		if (event) {
			data_reply (event, E_REQUESTPENDING, PianoErrorToStr (pRet));
		}
		/* Transient error; retry login again later. */
		app->retry_login_time = time(NULL) + app->settings.pandora_retry;
		return;
	}

	/* On success, update credentials on file */
	destroy_pandora_credentials(&app->settings.pandora);
	app->settings.pandora = app->settings.pending;
	memset (&app->settings.pending, 0, sizeof (app->settings.pending));
	set_ownership_rule (app->settings.pandora.manager_rule, app->settings.pandora.manager);
    save_pandora_credentials (&app->settings.pandora);
    if (changed) {
        send_response_code (app->service, I_SERVER_STATUS, "Pandora credentials changed.");
    }
	announce_privileges (app->service, NULL);
	/* Reset cache so we pull a new station list. */
	app->update_station_list = 0;
	update_station_list (app);
    if (!app->selected_station) {
        app->automatic_stations = false;
    }
	event_occurred (app->service, EVENT_AUTHENTICATED, S_OK);
}




/* Given a list of stations in an argv-style array,
   verify that all the stations exist.
   Return true on success, false on failure.
   Sends informational error details.
*/
bool validate_station_list (APPSTATE *app, FB_EVENT *event, char * const*stations) {
	assert (app);
	assert (event);
	assert (stations);

	bool response = true;
	PianoStation_t *station;
	while (*stations) {
		if ((station = PianoFindStationByName (app->ph.stations, *stations))) {
			if (station->isQuickMix) {
				send_data (event, I_STATION_INVALID, station->name);
				response = false;
			}
		} else {
			send_data (event, I_NOTFOUND, *stations);
			response = false;
		}
		stations++;
	}
	return response;
}



/* Given a song id, find the song it refers to.
   If a song id is not included, use the current song.
   Sends error response if song is not found/no current song.
 */
PianoSong_t *get_song_by_id_or_current (APPSTATE *app, FB_EVENT *event, const char *songid) {
	assert (app);
	assert (event);

	PianoSong_t *song = NULL;
	if (songid) {
		/* Referring to a song in the history, the queue, or current */
		song = PianoFindSongById (app->song_history, songid);
		if (!song) {
		    song = PianoFindSongById(app->playlist, songid);
			if (!song) {
				song = PianoFindSongById(app->current_song, songid);
				if (!song) {
					reply (event, E_NOTFOUND);
				}
			}
		}
	} else {
		/* Referring to the current song */
		song = app->current_song;
		if (!song) {
			reply (event, E_WRONG_STATE);
		}
	}
	return song;
}


/* Given a station name, find the station it refers to.
   If the station is not included, use the selected station
   if it matches the current song playing.
   Otherwise, sends applicable error response. */
PianoStation_t *get_station_by_name_or_current (APPSTATE *app, FB_EVENT *event, const char *stationname) {
	assert (app);
	assert (event);
	PianoStation_t *station = NULL;
	if (stationname) {
		station = PianoFindStationByName(app->ph.stations, stationname);
		if (!station) {
			reply (event, E_NOTFOUND);
		}
	} else if (app->selected_station && app->current_song) {
		if (strcmp (app->current_song->stationId, app->selected_station->id) == 0) {
			station = app->selected_station;
		} else {
			data_reply (event, E_CONFLICT, "Selected station is not playing station.");
		}
	} else {
		/* No selected station or presently intertrack */
		reply (event, E_WRONG_STATE);
	}
	return station;
}

/* Take possession of a station, if not already owned.  This enables
   customization of the station. */
bool pwn_station (APPSTATE *app, FB_EVENT *event, const char *stationId) {
	assert (app);
	assert (event);
	assert (stationId);

	PianoStation_t *station = PianoFindStationById (app->ph.stations, stationId);
	if (!station) {
		data_reply (event, I_NOTFOUND, "Station not found");
		flog (LOG_ERROR, "Station %s not found", stationId);
		return false;
	}
	if (!station->isCreator) {
		if (!piano_transaction (app, NULL, PIANO_REQUEST_TRANSFORM_STATION, station)) {
			reply (event, E_TRANSFORM_FAILED);
			return false;
		}
		flog (LOG_GENERAL, "Station %s has been personalized", station->name);
		send_status (event, "Station has been personalized");
	}
	return true;
}


/* Manage skip counts/frequency */
#define MAX_SKIPS (6)
#define SKIP_PERIOD (3600) /* 1 hour in seconds */
/* List structure for past skips.  They are stored most recent first. */
typedef struct skip_history_t {
	char *station;
	time_t when;
	struct skip_history_t *next;
} SKIP_HISTORY;

bool skips_are_available (APPSTATE *app, FB_EVENT *event, char *station) {
	assert (app);
	assert (event);
	assert (station);

	static SKIP_HISTORY *skip_history;
	bool skip_available = false;
	int skip_count = 0;
	time_t now = time (NULL);
	time_t valid_since = now - SKIP_PERIOD;
	SKIP_HISTORY *skip, *last;
	time_t first_skip_time = now;

	/* Scan the historic skips, counting for this station. */
	for (last = NULL, skip = skip_history; skip; last = skip, skip = skip->next) {
		/* If the skip is expired, forget it. */
		if (skip->when <= valid_since) {
			/* Since skips are stored in reverse chronological order,
			   all remaining skips will also be expired. */
			if (last) {
				last->next = NULL;
			} else {
				skip_history = NULL;
			}
			while ((last = skip)) {
				skip = skip->next;
				free (last->station);
				free (last);
			}
			break;
		} else {
			if (strcmp (skip->station, station) == 0) {
				first_skip_time = skip->when;
				skip_count++;
			}
		}
	}

	if (skip_count < MAX_SKIPS) {
		if ((skip = (SKIP_HISTORY *) calloc (sizeof (SKIP_HISTORY), 1))) {
			if ((skip->station = strdup (station))) {
				skip_available = true;
				skip_count++;
				skip->when = now;
				skip->next = skip_history;
				skip_history = skip;
			} else {
				data_reply(event, E_RESOURCE, strerror(errno));
				free (skip);
			}
		} else {
			data_reply(event, E_RESOURCE, strerror(errno));
		}
	}
	fb_fprintf (event, "%d Skip information: %d/%d used, first expires in %d seconds\n", I_INFO,
					skip_count, MAX_SKIPS, (int) SKIP_PERIOD - (now - first_skip_time));
	return (skip_available);
}


/* Resume playback without altering state is required.
 This is required in a few occasions such as skipping. */
void cancel_playback (APPSTATE *app) {
	if (app->player.mode >= PLAYER_STARTING &&
		app->player.mode < PLAYER_FINISHED_PLAYBACK) {
		/* If the player is paused, it must be resumed to get the player thread to shutdown. */
		/* There's a song actively playing. Resume it. */
		int err = pthread_mutex_lock (&app->player.pauseMutex);
		if (err == 0) {
			app->player.doQuit = 1;
			pthread_cond_broadcast (&app->player.pauseCond);
			pthread_mutex_unlock (&app->player.pauseMutex);
		} else {
			flog (LOG_ERROR, "cancel_playback:pthread_mutex_lock: %s", strerror (err));
		}
	}
	app->paused_since = 0;
}



/* Test audio output by making a 1kHz tone for a few seconds. */
#define AO_TEST_SAMPLE_FREQ (44100)
#define AO_TEST_DURATION (2)
#define AO_TEST_FREQUENCY (1000)
#define countof(x) (sizeof (x) / sizeof (*x))

void generate_test_tone (APPSTATE *app, FB_EVENT *event) {
	int audioOutDriver = -1;

	/* Find driver, or use default if unspecified. */
	audioOutDriver = app->settings.output_driver ? ao_driver_id (app->settings.output_driver)
					 : ao_default_driver_id();
	if (audioOutDriver < 0) {
		fb_fprintf (event, "%03d audio driver '%s' not found\n",
							E_NAK,
							app->settings.output_driver ? app->settings.output_driver : "(default)");
		return;
	}

	ao_sample_format format;
	memset (&format, 0, sizeof (format));
	format.bits = 16;
	format.channels = 2;
	format.rate = AO_TEST_SAMPLE_FREQ;
	format.byte_format = AO_FMT_NATIVE;

	/* Create a list of ao_options */
	ao_option *options = NULL;
	ao_append_option(&options, "client_name", PACKAGE);
	if (app->settings.output_device) {
		ao_append_option (&options, "dev", app->settings.output_device);
	}
	if (app->settings.output_id) {
		ao_append_option (&options, "id", app->settings.output_id);
	}
	if (app->settings.output_server) {
		ao_append_option (&options, "server", app->settings.output_server);
	}

	ao_device *device = ao_open_live (audioOutDriver, &format, options);
	if (device == NULL) {
		fb_fprintf (event,
				  "%03d Cannot open audio device %s/%s/%s: %s\n",
				  E_NAK,
				  app->settings.output_device ? app->settings.output_device : "default",
				  app->settings.output_id ? app->settings.output_id : "default",
				  app->settings.output_server ? app->settings.output_server : "default",
				  errno == AO_ENODRIVER ? "No driver" :
				  errno == AO_ENOTLIVE ? "Not a live output device" :
				  errno == AO_EBADOPTION ? "Bad option" :
				  errno == AO_EOPENDEVICE ? "Cannot open device" : "Other failure");
		return;
	}
	ao_free_options (options);

	/* Create the test tone. */
	int16_t tone [AO_TEST_SAMPLE_FREQ * AO_TEST_DURATION * 2];
	for (int i = 0; i < countof (tone); i++) {
		tone [i] = (int16_t)(32767.0 *
							sinf(2 * M_PI * AO_TEST_FREQUENCY * ((float) i/AO_TEST_SAMPLE_FREQ)));
	}

	ao_play(device, (char *) tone, sizeof (tone));
	ao_close(device);
	reply (event, S_OK);
}
