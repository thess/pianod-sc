/*
 *  response.h
 *  pianod
 *
 *  Created by Perette Barella on 2012-03-16.
 *  Copyright 2012-2014 Devious Fish. All rights reserved.
 *
 */

#include <fb_public.h>
#include "pianod.h"
#include "seeds.h"
#include "command.h"
#include "users.h"

#ifndef _RESPONSE_H
#define _RESPONSE_H

typedef enum server_status_t {
	/* Informational messages, can occur anytime */
	I_WELCOME = 100,
	I_PLAYING = 101, /* 101-109 Playback status */
	I_PAUSED = 102,
	I_STOPPED = 103,
	I_BETWEEN_TRACKS = 104,
	I_TRACK_COMPLETE = 105,
	I_STALLED = 106,
	I_SELECTEDSTATION_NONE = 108,
	I_SELECTEDSTATION = 109,
	I_ID = 111, /* Song ID */ /* 111-129 Station/artist/track field ids */
	I_ALBUM = 112,
	I_ARTIST = 113,
	I_SONG = 114,
	I_STATION = 115,
	I_RATING = 116,
	I_INFO_URL = 117,
	I_COVERART = 118,
	I_GENRE = 119,
	I_USERRATING = 120,
	I_CHOICEEXPLANATION = 121,
	I_YELL = 131, /* Misc */
	I_INFO = 132,
	I_SERVER_STATUS = 133,
	I_MIX_CHANGED = 134,
	I_STATIONS_CHANGED = 135,
	I_USER_PRIVILEGES = 136,
	I_USERRATINGS_CHANGED = 137,
	/* pianod settings */
	I_VOLUME = 141,
	I_HISTORYSIZE = 142,
	I_AUDIOQUALITY = 143,
	I_AUTOTUNE_MODE = 144,
	I_PAUSE_TIMEOUT = 146,
	I_PLAYLIST_TIMEOUT = 147,
	/* Pandora communication settings */
	I_PROXY = 161,
	I_CONTROLPROXY = 162,
	I_RPCHOST = 163,
	I_RPCTLSPORT = 164,
	I_PARTNERUSER = 165,
	I_PARTNERPASSWORD = 166,
	I_PANDORADEVICE = 167,
	I_ENCRYPTION_PASSWORD = 168,
	I_DECRYPTION_PASSWORD = 169,
	I_PANDORA_USER = 170,
	I_PANDORA_PASSWORD = 171,
	I_TLSFINGERPRINT = 172,
	I_OUTPUT_DRIVER = 181,
	I_OUTPUT_DEVICE = 182,
	I_OUTPUT_ID = 183,
	I_OUTPUT_SERVER = 184,
#if defined(ENABLE_CAPTURE)
	I_CAPTUREPATH = 190,
#endif
#if defined(ENABLE_SHOUT)
	I_SHOUTCAST = 191,
#endif
	/* Status messages, exactly one occurs (except for lists, as noted below) in response to commands */
	S_OK = 200,
	S_ANSWER_YES = 201,
	S_ANSWER_NO = 202,
	S_DATA = 203, /* For multi line items: Occurs 0 or more times, once before each listed group. */
				  /* For single line items: occurs once */
	S_DATA_END = 204, /* Occurs exactly once after last list item */
	S_SIGNOFF = 205,
	/* Error information.  These occur multiple times per command, with a 400 series following. */
	I_ERROR_DETAIL = 300,
	I_PROXY_CONFIG = 301,
	I_STATION_INVALID = 303, /* Can't use the quickmix for that */
	I_NOTFOUND = 304,
	/* User failures, like unauthorized, bad commands, the connection is down, etc.
	 These will occur in response to commands, one per command. */
	E_BAD_COMMAND = 400,
	E_UNAUTHORIZED = 401,
	E_NAK = 402, /* negative acknowledgement */
	E_DUPLICATE = 403,
	E_NOTFOUND = 404,
	E_WRONG_STATE = 405,
	E_CREDENTIALS = 406,
	E_INVALID = 407,
	E_TRANSFORM_FAILED = 408,
	E_CONFLICT = 409,
	E_REQUESTPENDING = 410, /* Request couldn't be completed now, we'll try again later. */
	E_QUOTA = 411, /* Quota restriction encountered. */
	E_LOGINREQUIRED = 412, /* Command/feature requires user be logged in */
	/* Server failures, like out of memory, etc.
	 Not a response to a particular command, although a command may initiate what causes them */
	E_NOT_IMPLEMENTED = 499,
	E_FAILURE = 500,
	E_NETWORK_FAILURE = 502,
	E_SHUTDOWN = 503,
	E_AUTHENTICATION = 504,
	E_RESOURCE = 505,
	E_PANDORA = 507,
	E_INCOMPLETE = 508,
	/* Action messages.  These are used to lookup text, in case we want to internationalize messaging. */
	A_SIGNED_IN = 1000,
	A_SIGNED_OUT = 1001,
	A_KICKED = 1002,
	A_SKIPPED = 1010,
	A_STOPPED = 1011,
	A_PAUSED = 1012,
	A_RESUMED = 1013,
	A_CHANGED_MIX = 1014,
	A_MIX_ADDED = 1015,
	A_MIX_REMOVED = 1016,
	A_SELECTED_STATION = 1020,
	A_CREATED_STATION = 1021,
	A_RENAMED_STATION = 1022,
	A_DELETED_STATION = 1023,
    A_PANDORA_SET = 1030,
    A_PANDORA_BORROW = 1031
} RESPONSE_CODE;

/* Replies to events */
extern void reply (FB_EVENT *event, const RESPONSE_CODE status);
extern void data_reply (FB_EVENT *event, const RESPONSE_CODE status, const char *detail);

/* Broadcast statuses and events */
extern void send_response (void *there, RESPONSE_CODE code);
extern void send_response_code (void *there, const RESPONSE_CODE code, const char *message);
extern void send_status (void *there, const char *message);
extern void send_data (void *there, const RESPONSE_CODE dataitem, const char *data);
extern void send_selectedstation (void *there, APPSTATE *app);
extern void announce_action (FB_EVENT *there, APPSTATE *app, RESPONSE_CODE code, const char *parameter);

/* Send back application-related messages */
extern void send_station_list (void *there, const PianoStation_t *station, const COMMAND cmd);
extern void send_song_list (FB_EVENT *event, const APPSTATE *app, const PianoSong_t *song);
extern void send_songs_or_details (FB_EVENT *event, const APPSTATE *app, const PianoSong_t *song,
									  STATION_INFO_TYPE songtype);
extern void send_playback_status (void *there, APPSTATE *app);
extern void send_song_info (void *there, const APPSTATE *app, const PianoSong_t *song);
extern void send_song_rating (void *there, const PianoSong_t *song);
extern void send_artists (void *there, const PianoArtist_t *artist, STATION_INFO_TYPE songtype);

/* This may get moved into football at some point */
extern char *Response (RESPONSE_CODE code);

#endif /* _RESPONSE_H_ */
