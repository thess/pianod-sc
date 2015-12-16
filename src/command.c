/*
 *  service.c
 *  Server-style UI for libpiano.
 *
 *  Created by Perette Barella on 2012-03-10.
 *  Copyright 2012-2014 Devious Fish. All rights reserved.
 *  Parts liberally plagiarized and adapted from PianoBar
 *  Copyright (c) 2008-2011 Lars-Dominik Braun <lars@6xq.net>
 *
 */

#ifndef __FreeBSD__
#define _POSIX_C_SOURCE 1 /* fileno() */
#define _BSD_SOURCE /* strdup() */
#define _DARWIN_C_SOURCE /* strdup() on OS X */
#endif

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include <getopt.h>
#include <limits.h>
#include <ctype.h>
#include <dirent.h>

#include <ao/ao.h>
#include <fb_public.h>
#include <piano.h>

#include "command.h"
#include "support.h"
#include "pianoextra.h"
#include "response.h"
#include "pianod.h"
#include "logging.h"
#include "pianod.h"
#include "seeds.h"
#include "query.h"
#include "users.h"
#include "tuner.h"

#define countof(x) (sizeof (x) / sizeof (*x))

#define RANK_PATTERN "<disabled|listener|user|admin>"
#define PRIV_PATTERN "<service|influence|tuner>"

static FB_PARSE_DEFINITION loginstatements[] = {
	{ HELP,				"help [{command}]" },				/* Request help */
	{ QUIT,				"quit" },							/* Logoff terminal */
	{ AUTHENTICATE,		"user {username} {password}" },		/* Authenticate */
	{ AUTHANDEXEC,		"as user {username} {password} {command} ..." },
															/* Authenticate and execute command. */
	{ GETUSERRANK,		"get privileges" }					/* Request user's level/privileges */
};

static FB_PARSE_DEFINITION listenerstatements[] = {
	{ TIMESTATUS,		"" },								/* On null input, display time. */
	{ NOP,				"# ..." },							/* Comment */
	{ HELP,				"? [{command}]" },
	{ QUERYSTATUS,		"status" },							/* Current song, etc. */
	{ QUERYHISTORY,		"history [{#index}]" },				/* Previously played songs */
	{ QUERYQUEUE,		"queue [{#index}]" },				/* Show upcoming songs */
	{ YELL,				"yell {announcement}" },			/* Broadcast to all connected terminals */
	{ GETHISTORYSIZE,	"get history length" },				/* Read the length of the history */
	{ GETAUDIOQUALITY,	"get audio quality" },				/* Read the audio quality */
	{ AUTOTUNEGETMODE,	"autotune mode" },					/* Read the autotuning mode */
	{ GETVOLUME,		"volume" },							/* Query volume level */
	{ SETMYPASSWORD,	"set password {old} {new}" },		/* Let a user update their password */
	{ WAITFORENDOFSONG,	"wait for end of song" },			/* Delay further input until after song ends */
	{ WAITFORNEXTSONG,	"wait for next song" }				/* Delay further input until next song begins */
};

static FB_PARSE_DEFINITION influencestatements[] = {
    /* We don't prevent a guest using these commands, but they don't do
       much without influence privilege.  Don't show without influence. */
	{ STATIONRATE,		"rate station <good|bad|neutral> [{station}]" }, /* Rate a station */
	{ STATIONRATINGS,	"station ratings [{station}]" }     /* Station ratings */
};

static FB_PARSE_DEFINITION userstatements[] = {
	{ NEXTSONG,			"skip" },							/* Skip the rest of the current song */
	{ PAUSEPLAYBACK,	"pause" },							/* Pause playback */
	{ STOPPLAYBACK,		"stop [now]" },						/* Stop playback when song finished */
	{ PLAY,				"play" },							/* Start/resume playback */
	{ PLAYPAUSE,		"playpause" },						/* Toggle music playback */
	{ PLAYQUICKMIX,		"play <mix|auto>" },				/* Select the quickmix station, start playback */
	{ PLAYSTATION,		"play station {station}" },			/* Select a station, start playback */
	{ SELECTQUICKMIX,	"select <mix|auto>" },				/* Select the quickmix station */
	{ SELECTSTATION,	"select station {station}" },		/* Select a station */
	{ STATIONLIST,		"stations [list]" },				/* List the stations */
	{ STATIONINFO,		"station seeds [{station}]" },		/* Dump station seed/feedback details */
	{ QUICKMIXINCLUDED,	"mix" },							/* Short form; not official protocol */
	{ QUICKMIXINCLUDED, "mix list" },						/* Medium; not official protocol */
	{ QUICKMIXINCLUDED, "mix list included" },				/* Long */
	{ QUICKMIXEXCLUDED, "mix list excluded" },				/* Show songs not included in quickmix */
	{ QUICKMIXADD,		"mix add {station} ..." },			/* Put 1 or more stations in the quickmix */
	{ QUICKMIXDROP,		"mix remove {station} ..." },		/* Remove 1 or more stations from the quickmix */
	{ QUICKMIXSET,		"mix set {station} ..." },			/* Set the quickmix to a list of stations */
	{ QUICKMIXTOGGLE,	"mix toggle {station} ..." },		/* Toggle stations in/out of the quickmix */
	{ GETSUGGESTIONS,	"find <any|song|artist|genre> [{specifier}]" },
															/* Find anything - Prototype */
	{ SETVOLUME,		"volume {level}" }					/* Directly set the volume level */
};

static FB_PARSE_DEFINITION adminstatements[] = {
	{ SETAUDIOQUALITY,	"set audio quality <high|medium|low>" },		/* Set the audio quality */
	{ GETPANDORAUSER,	"get pandora user" },							/* Get Pandora account user/password */
	{ GETRPCHOST,		"get rpc host" },								/* Read the RPC host */
	{ SETRPCHOST,		"set rpc host {hostname}" },					/* libwaitress/libpiano setting */
	{ GETRPCTLSPORT,	"get rpc tls port" },							/* Read the RPC TLS settings */
	{ SETRPCTLSPORT,	"set rpc tls port [{port}]" },					/* libwaitress/libpiano setting */
	{ GETPARTNER,		"get partner" },								/* Read the partner user */
	{ SETPARTNER,		"set partner {user} {password}" },				/* libwaitress/libpiano setting */
	{ GETPANDORADEVICE,	"get pandora device" },							/* Read the Pandora device type */
	{ SETPANDORADEVICE,	"set pandora device {devicetype}" },			/* libwaitress/libpiano setting */
	{ GETENCRYPTPASSWORD, "get encryption password" },					/* libwaitress/libpiano setting */
	{ SETENCRYPTPASSWORD, "set encryption password {password}" },		/* libwaitress/libpiano setting */
	{ GETDECRYPTPASSWORD, "get decryption password" },					/* libwaitress/libpiano setting */
	{ SETDECRYPTPASSWORD, "set decryption password {password}" },		/* libwaitress/libpiano setting */
	{ GETTLSFINGERPRINT, "get tls fingerprint" },						/* libwaitress/libpiano setting */
	{ SETTLSFINGERPRINT, "set tls fingerprint {fingerprint}" },			/* libwaitress/libpiano setting */
	{ GETOUTPUTDRIVER,	"get audio output driver" },					/* libao setting */
	{ SETOUTPUTDRIVER,	"set audio output driver [{driver}]" },			/* libao setting */
	{ GETOUTPUTDEVICE,	"get audio output device" },					/* libao setting */
	{ SETOUTPUTDEVICE,	"set audio output device [{device}]" },			/* libao setting */
	{ GETOUTPUTID,		"get audio output id" },						/* libao setting */
	{ SETOUTPUTID,		"set audio output id [{#id}]" },				/* libao setting */
	{ GETOUTPUTSERVER,	"get audio output server" },					/* libao setting */
	{ SETOUTPUTSERVER,	"set audio output server [{server}]" },			/* libao setting */
	{ TESTAUDIOOUTPUT,	"test audio output" },							/* Output a test tone */
#if defined(ENABLE_CAPTURE)
	{ GETCAPTUREPATH,	"get capture" },
	{ SETCAPTUREPATH,	"set capture <path|off> [{path}]" },
#endif
#if defined(ENABLE_SHOUT)
	{ SETSHOUTCAST,		"set shoutcast <on|off>" },
#endif
	{ SETLOGGINGFLAGS,	"set [football] logging flags {#logging-flags:0x0-0xffff}" },
                                                                        /* Pianod or football debug logging flags */
	{ GETPROXY,			"get proxy" },									/* Read the proxy for all accesses */
	{ SETPROXY,			"set proxy {url}" },							/* Set the proxy for all accesses */
	{ GETCONTROLPROXY,	"get control proxy" },							/* Read the proxy for control connection */
	{ SETCONTROLPROXY,	"set control proxy {url}" },					/* Set the proxy for control connection only */
	{ GETPAUSETIMEOUT,	"get pause timeout" },							/* Duration before paused track is killed */
	{ SETPAUSETIMEOUT,	"set pause timeout {#duration:15-86400}" },		/* Set aforementioned duration */
	{ GETPANDORARETRY,	"get pandora retry" },		/* Login retry timeout */
	{ SETPANDORARETRY,	"set pandora retry {#duration:5-300}" },
	{ GETPLAYLISTTIMEOUT, "get playlist timeout" },						/* Duration before playlist expires */
	{ SETPLAYLISTTIMEOUT, "set playlist timeout {#duration:1800-86400}" },
                                                                        /* Set aforementioned duration */
	{ SETHISTORYSIZE,	"set history length {#length:1-50}" },			/* Set the length of the history */
	{ SETVISITORRANK,	"set visitor rank " RANK_PATTERN },				/* Visitor privilege level */
	{ AUTOTUNESETMODE,	"autotune mode <login|flag|all>" },				/* Which method to autotune by */
	{ SHOWUSERACTIONS,	"announce user actions <on|off>" },				/* Whether to broadcast events */
	{ SHUTDOWN,			"shutdown" },									/* Shutdown the player and quit */
	{ USERCREATE,		"create <listener|user|admin> {user} {passwd}" },	/* Add a new user */
	{ USERSETPASSWORD,	"set user password {user} {password}" },		/* Change a user's password */
	{ USERSETRANK,		"set user rank {user} " RANK_PATTERN },         /* Alter rank */
	{ USERDELETE,		"delete user {user}" },							/* Remove a user */
	{ USERGRANT,		"grant " PRIV_PATTERN " to {user} ..." },
																		/* Grant privilege */
	{ USERREVOKE,		"revoke " PRIV_PATTERN " from {user} ..." },
																		/* Revoke privilege */
    { USERLISTBYPRIVILEGE, "users with <owner|service|influence|tuner|present>" },
                                                                        /* List users with a privilege */
	{ USERLIST,			"users list [{user}]" },						/* List all or a specific user */
	{ USERSONLINE,		"users online" },								/* List users logged in */
	{ USERKICK,			"kick user {user} [{message}]" },				/* Log a user off */
	{ USERKICKVISITORS,	"kick visitors [{message}]" }					/* Disconnect unauthenticated */
};

static FB_PARSE_DEFINITION ownerstatements[] = {
	{ RATELOVE,			"rate good [{songid}]" },						/* Thumbs up */
	{ RATEHATE,			"rate bad [{songid}]" },						/* Thumbs down */
	{ RATENEUTRAL,		"rate neutral [{songid}]" },					/* Remove track feedback */
	{ RATEOVERPLAYED,	"rate overplayed [{songid}]" },					/* One month ban */
	{ STATIONCREATEBYSONG,
		"create station from <song|artist> [{songid}]" },				/* Create station from track */
	{ STATIONCREATEBYSONGWNAME,
		"create station named {name} from <song|artist> [{songid}]" },	/* Create station from track */
	{ STATIONCREATE,
		"create station from suggestion {suggestionid}" },				/* Create station */
	{ STATIONCREATEWNAME,
		"create station named {name} from suggestion {suggestionid}" },	/* Create station */
	{ STATIONCREATEBYID,"create station from shared {stationId}"},		/* Import a shared station */
	{ STATIONCREATEBYIDWNAME,
		"create station named {name} from shared {stationId}"},			/* Import a shared station */
	{ STATIONRENAME,	"rename station {station} to {newname}" },		/* Rename a station */
	{ STATIONDELETE,	"delete station {station}" },					/* Delete a station */
	{ SEEDADD,			"add seed from suggestion {suggestionid}" },	/* Add a seed to current station */
	{ SEEDADDWSTATION,
		"add seed from suggestion {suggestionid} to {station}" },		/* Add a seed to a station */
	{ SEEDADDBYSONG,	"add <song|artist> seed from song [{songid}]" },/* Add a seed for playing song */
	{ SEEDADDBYSONGWSTATION,
		"add <song|artist> seed to {station} from song [{songid}]" },	/* Variant w/ station */
	{ SEEDDELETEBYSONG,	"delete <song|artist> seed [{songid}]" },		/* De-seed song or artist. */
	{ SEEDDELETEBYID,	"delete seed {seedid}" },						/* Remove seed/rating by id. */
	{ EXPLAINSONGCHOICE,"explain song [{songid}]" },					/* Query why Pandora chose a song */
	{ CREATEBOOKMARK,	"bookmark {song|artist} for song [{songid}]" }	/* Pandora allows bookmarking via a song */
};


static FB_PARSE_DEFINITION servicestatements[] = {
	{ PANDORAUSER,		"[remember] pandora user {user} {passwd} [mine|unowned]" },
                                                                        /* Set Pandora password username/password */
	{ PANDORAUSERSPECIFY,
						"pandora user {user} {passwd} managed by {user}" }, /* Same w/ specific user */
    { PANDORAEXISTING,  "pandora use {user}" },                         /* Set user's Pandora credentials */
	{ WAITFORAUTHENTICATION,
						"wait for authentication" },					/* Pause commands until auth succeeds */
    { USERLISTPANDORA,  "pandora list users" },                         /* Get list of users with creds */
};

static FB_PARSE_DEFINITION tunerstatements[] = {
	{ AUTOTUNEUSERS,	"autotune for ..." },							/* Tune for a list of users. */
    { AUTOTUNEUSERSLIST,"autotune list users" },                        /* List users considered by autotuning */
	{ AUTOTUNEADDREMOVE,"autotune <consider|disregard> {user} ..." }	/* Add users to tuning list. */
};

static FB_PARSE_DEFINITION deprecatedstatements[] = {
    /* Changed 'guest' to 'visitor', 'level' to 'rank' mid-2013.
       Support old form through July 1, 2014. */
#define OLD_RANK_PATTERN "<disabled|guest|user|admin>"
	{ SETVISITORRANK,	"set guest level " OLD_RANK_PATTERN },      /* Set guest rank */
	{ USERSETRANK,		"set user level {user} " OLD_RANK_PATTERN }, /* Alter rank */
    /* Changed 'guest' rank to 'listener' January 2014.
     Support old form through December 31, 2014. */
	{ SETVISITORRANK,	"set visitor rank guest" },				/* Visitor privilege level */
	{ USERCREATE,		"create guest {user} {passwd}" },       /* Add a new user */
	{ USERSETRANK,		"set user rank {user} guest " },        /* Alter rank */
	{ PANDORAUSERSPECIFY,
        "pandora user {user} {passwd} owned by {user}" }        /* Same w/ specific user */
};

/* Create a fake rank for handling owner privs */
#define NOT_APPLICABLE (-1)

static struct commandset_t {
	USER_RANK usertype;
	PRIVILEGE privilege;
	FB_PARSE_DEFINITION *commandset;
	size_t count;
} allstatements [] = {
	{ RANK_NONE,			NOT_APPLICABLE,		loginstatements,	countof (loginstatements) },
	{ RANK_LISTENER,		NOT_APPLICABLE,		listenerstatements,	countof (listenerstatements) },
    { NOT_APPLICABLE,       PRIVILEGE_INFLUENCE,influencestatements,countof(influencestatements) },
	{ RANK_STANDARD,		NOT_APPLICABLE,		userstatements,		countof (userstatements) },
	{ RANK_ADMINISTRATOR,	NOT_APPLICABLE,		adminstatements,	countof (adminstatements) },
	{ NOT_APPLICABLE,		PRIVILEGE_MANAGER,	ownerstatements,	countof (ownerstatements) },
	{ RANK_ADMINISTRATOR,	PRIVILEGE_SERVICE,	servicestatements,	countof (servicestatements) },
	{ RANK_ADMINISTRATOR,	PRIVILEGE_TUNER,	tunerstatements,	countof (tunerstatements) },
	{ NOT_APPLICABLE,		NOT_APPLICABLE,		deprecatedstatements,countof (deprecatedstatements) }
};


/* Display help */
static void send_statement_list (FB_EVENT *event, struct user_t *user, const char *topic) {
	int j;
	unsigned long topiclen = topic ? strlen (topic) : 0;
	for (j = 0; j < countof (allstatements); j++) {
		bool show = false;
		if (allstatements [j].usertype != (USER_RANK) NOT_APPLICABLE) {
			show = have_rank (user, allstatements [j].usertype);
		}
		if (allstatements [j].privilege != (PRIVILEGE) NOT_APPLICABLE) {
			show = show || have_privilege (user, allstatements [j].privilege);
		}
		if (show) {
			int i;
			for (i = 0; i < allstatements [j].count; i++) {
				const char *command = allstatements [j].commandset[i].statement;
				if (topic == NULL || strncasecmp (command, topic, topiclen) == 0) {
					send_response_code (event, I_INFO, allstatements [j].commandset[i].statement);
				}
			}
		}
	}
}


/* Set, add, remove, or toggle stations in the mix */
static RESPONSE_CODE manipulate_quickmix (APPSTATE *app, FB_EVENT *event,
										  const COMMAND cmd, char *const *argv) {
	PianoStation_t *station = app->ph.stations;
	RESPONSE_CODE response = S_OK;
	RESPONSE_CODE action;
	int change_count = 0;
	char *change_name;
	if (cmd == QUICKMIXSET) {
		/* Set all to not included */
		PianoListForeachP (station) {
			station->useQuickMix = false;
		}
		change_count = 2; /* Force generic change message */
	}
	while (*argv) {
		station = PianoFindStationByName(app->ph.stations, *argv);
		assert (station);
		if (station) {
			assert (!station->isQuickMix);
			bool old = station->useQuickMix;
			switch (cmd) {
				case QUICKMIXSET:
				case QUICKMIXADD:
					station->useQuickMix = true;
					break;
				case QUICKMIXDROP:
					station->useQuickMix = false;
					break;
				case QUICKMIXTOGGLE:
					station->useQuickMix = !station->useQuickMix;
					break;
				default: assert (0);
			}
			if (old != station->useQuickMix) {
				change_count++;
				change_name = *argv;
				action = station->useQuickMix ? A_MIX_ADDED : A_MIX_REMOVED;
			}
		} else {
			flog (LOG_ERROR, "manipulate_quickmix: Station not in list anymore\n");
		}
		argv++;
	}
	if (change_count > 0) {
		if (piano_transaction (app, NULL, PIANO_REQUEST_SET_QUICKMIX, NULL)) {
			send_response (event->service, I_MIX_CHANGED);
			announce_action (event, app, change_count == 1 ? action : A_CHANGED_MIX,
							 change_count == 1 ? change_name : NULL);
		} else {
			response = E_NAK;
		}
	}
	return response;
}


/* Send the queue or history.  Allow negative indexes from either to refer to the other;
   0 refers to current track.  This makes implementing paging on clients easier because
   it won't have to use separate commands. */
static void send_song_lists (APPSTATE *app, FB_EVENT *event, COMMAND cmd) {
	if (event->argc == 1) {
		/* No index specified, send the whole list. */
		send_song_list (event, app, cmd == QUERYHISTORY ? app->song_history : app->playlist);
	} else {
		long index = atoi(event->argv [1]);
		if (index == 0) {
			if (app->current_song) {
				send_song_list (event, app, app->current_song);
			} else {
				reply (event, E_WRONG_STATE);
			}
		} else {
			PianoSong_t *song = ((cmd == QUERYHISTORY) == (index > 0)) ? app->song_history : app->playlist;
			if (index < 0) {
				index = -index;
			}
			while (--index > 0 && song) {
				song = PianoListNextP (song);
			}
			if (song) {
				reply (event, S_DATA);
				send_song_info (event, app, song);
				send_station_rating (event, song->stationId);
				reply (event, S_DATA_END);
			} else {
				reply (event, E_NOTFOUND);
			}
		}
	}
}

/* Start, stop, and toggle playback.  The mutex used to pause the player
 isn't a reliable way to check status, we we'll record status in the
 application state and tell the mutex what to do based on that. */
static void control_playback (APPSTATE *app, FB_EVENT *event, COMMAND cmd) {
	PLAYBACK_STATE orig_state = app->playback_state;
	/* Determine the new state */
	switch (cmd) {
		case PLAY:
			app->playback_state = PLAYING;
			break;
		case PAUSEPLAYBACK:
			app->playback_state = PAUSED;
			break;
		case PLAYPAUSE:
			app->playback_state = (app->playback_state == PLAYING ? PAUSED : PLAYING);
			break;
        default:
            assert (0);
            return;
	}

	/* Apply that state to the player. */
	if (app->player.mode >= PLAYER_STARTING) {
		/* The player thread is active. */
		/* There may or may not be a station (if not, we're running out the current track); we don't care. */
		/* Lock the mutex and change the thread's state. */
		int err;
		/* Ensure we have a mutex before we try to release it. */
		err = pthread_mutex_lock (&app->player.pauseMutex);
		if (err == 0) {
			app->player.doPause = (app->playback_state == PAUSED);
			pthread_cond_broadcast (&app->player.pauseCond);
			pthread_mutex_unlock (&app->player.pauseMutex);
			send_playback_status (app->service, app);
			reply (event, S_OK);
		} else {
			perror ("control_playback:pthread_mutex_lock");
			data_reply(event, E_FAILURE, strerror (err));
			reply (event, E_NAK);
		}
		if (app->playback_state == PAUSED && !app->paused_since) {
			app->paused_since = time (NULL);
		} else if (app->playback_state == PLAYING) {
			app->paused_since = 0;
		}
	} else if (!app->selected_station) {
		/* The player isn't playing and there's no current station. */
		data_reply(event, E_WRONG_STATE, "No station selected");
		app->playback_state = orig_state;
	} else {
		/* There's an active station but the player thread is in a transitional state.
		 Setting the playback_state thread should shall have done the magic by regulating
		 whether the playback thread restarts anew from the main run loop. */
		reply (event, S_OK);
	}
	if (app->playback_state == PAUSED) {
		/* Reset stall data so we don't count pause as a stall */
		memset (&app->stall, 0, sizeof (app->stall));
	}
	return;
}


/* New stations are added to the end of the station list.  Rename the last
   station to a desired name. */
static void rename_new_station (APPSTATE *app, FB_EVENT *event, char *to_name) {
	if (to_name && app->ph.stations) {
		/* Find the last station and own it. */
		PianoStation_t *station = app->ph.stations;
		while (PianoListNextP (station)) {
			station = PianoListNextP (station);
		}
		if (pwn_station (app, event, station->id)) {
			PianoRequestDataRenameStation_t reqData;
			memset (&reqData, 0, sizeof (reqData));

			reqData.station = station;
			reqData.newName = to_name;

			if (!piano_transaction (app, NULL, PIANO_REQUEST_RENAME_STATION, &reqData)) {
				send_response_code (event, E_INCOMPLETE, "Station created but retains default name");
			}
		}
	}
}



static void create_station_from_song (APPSTATE *app, FB_EVENT *event,
									  char *name, bool artist, const char *songid) {
	PianoSong_t *song;

	if ((song = get_song_by_id_or_current (app, event, songid))) {
		PianoRequestDataCreateStation_t reqData;
		reqData.token = song->trackToken;
		reqData.type = artist ? PIANO_MUSICTYPE_ARTIST : PIANO_MUSICTYPE_SONG;
		if (piano_transaction (app, event, PIANO_REQUEST_CREATE_STATION, &reqData)) {
			rename_new_station (app, event, name);
			send_response (event->service, I_STATIONS_CHANGED);
			announce_action (event, app, A_CREATED_STATION, name);
		}
	}
}



static void create_station_from_shared (APPSTATE *app, FB_EVENT *event,
									  char *name, char *station_id) {
	/* Validate station id; should be all numeric */
	for (char *ch = station_id; *ch; ch++) {
		if (!isdigit (*ch)) {
			reply (event, E_INVALID);
			return;
		}
	}
	PianoRequestDataCreateStation_t reqData;
	reqData.token = station_id;
	reqData.type = PIANO_MUSICTYPE_INVALID;
	if (piano_transaction (app, event, PIANO_REQUEST_CREATE_STATION, &reqData)) {
		rename_new_station (app, event, name);
		send_response (event->service, I_STATIONS_CHANGED);
		announce_action (event, app, A_CREATED_STATION, name);
	}
}


static void create_station_from_suggestion (APPSTATE *app, FB_EVENT *event,
									  char *name, char *suggestionid) {
	PianoRequestDataCreateStation_t reqData;
	STATION_INFO_TYPE type = *(suggestionid++);
	if (type == INFO_SONGSUGGESTION || type == INFO_ARTISTSUGGESTION || type == INFO_GENRESUGGESTION) {
		reqData.token = suggestionid;
		reqData.type = PIANO_MUSICTYPE_INVALID;
		if (piano_transaction (app, event, PIANO_REQUEST_CREATE_STATION, &reqData)) {
			rename_new_station (app, event, name);
			send_response (event->service, I_STATIONS_CHANGED);
			announce_action (event, app, A_CREATED_STATION, name);
		}
	} else {
		reply (event, E_INVALID);
	}
}


static void rename_station (APPSTATE *app, FB_EVENT *event,
							  const char *from_name, char *to_name) {
	PianoStation_t *station = PianoFindStationByName (app->ph.stations, from_name);
	if (!station) {
		reply (event, E_NOTFOUND);
	}
	if (!pwn_station (app, event, station->id)) {
		return;
	}

	PianoRequestDataRenameStation_t reqData;
	memset (&reqData, 0, sizeof (reqData));

	reqData.station = station;
	reqData.newName = to_name;

	if (piano_transaction (app, event, PIANO_REQUEST_RENAME_STATION, &reqData)) {
		send_response (event->service, I_STATIONS_CHANGED);
		announce_action (event, app, A_RENAMED_STATION, from_name);
	}
}

static void explain_song_choice (APPSTATE *app, FB_EVENT *event, const char *song_id) {
	PianoRequestDataExplain_t reqData;
	memset (&reqData, 0, sizeof (reqData));

	if ((reqData.song = get_song_by_id_or_current(app, event, song_id))) {
		if (piano_transaction (app, NULL, PIANO_REQUEST_EXPLAIN, &reqData)) {
			reply (event, S_DATA);
			data_reply (event, I_CHOICEEXPLANATION, reqData.retExplain);
			reply (event, S_DATA_END);
			free (reqData.retExplain);
		} else {
			reply (event, E_NAK);
		}
	}
}



static void accept_new_credentials (APPSTATE *app, FB_EVENT *event, const char *manager) {
	USER_CONTEXT *context = event->context;
    USER *manageruser = NULL;
    char * const *argv = event->argv; /* Must not mangle event pointer */
    bool remember_credentials;
    if ((remember_credentials = (strcasecmp (argv [0], "remember") == 0))) {
        argv++;
    }
	if (((argv [4] && (strcasecmp (argv [4], "mine") == 0)) || remember_credentials) && !context->user) {
		/* Visitors may not self-claim or store credentials for a station */
		reply (event, E_LOGINREQUIRED);
	} else if (!manager || ((manageruser = get_user_by_name (event, manager)))) {
		CREDENTIALS *creds = &app->settings.pending;
		destroy_pandora_credentials (creds);
        if (remember_credentials) {
            creds->creator = context->user;
        }
		creds->username = strdup (argv [2]);
		creds->password = strdup (argv [3]);
		/* Stash the new crendentials, replacing previous new ones */
		if (manager) {
			creds->manager_rule = MANAGER_USER;
			creds->manager = manageruser;
		} else if (argv [4] && strcasecmp (argv [4], "mine") == 0) {
			creds->manager_rule = MANAGER_USER;
            creds->manager = context->user;
		} else if (argv [4]) {
			creds->manager_rule = MANAGER_NONE;
		} else {
			creds->manager_rule = MANAGER_ADMINISTRATOR;
		}
		if (creds->username && creds->password) {
            assert ((creds->manager_rule != MANAGER_USER || creds->manager));
			set_pandora_user (app, event);
		} else {
            destroy_pandora_credentials(creds);
			send_response_code (app->service, E_FAILURE, strerror (errno));
			reply (event, E_NAK);
		}
	}
}


static void authorize_and_execute (APPSTATE *app, FB_EVENT *event)
{
	/* Request the connection be closed.  Football won't close it until after
	   it delivers a connection close event, which won't occur until we read
	   from it.  Doing this now prevents this connection from being considered
	   in autotuning station calculations, reporting connected users, etc. */
	fb_close_connection (event->connection);
	USER_CONTEXT *user = (USER_CONTEXT *)event->context;
	if ((user->user = authenticate_user (event->argv[2], event->argv[3]))) {
		/* We have to mangle argv, so copy the event and change it there. */
		FB_EVENT child_event;
		memcpy (&child_event, event, sizeof (child_event));
		child_event.argv += 4;
		child_event.argc -= 4;
		if (strcasecmp (child_event.argv[0], "quit") == 0) {
			/* Executing quit would cause double-close of connection */
			reply (&child_event, S_OK);
		} else {
			execute_command(app, &child_event);
		}
		user->user = NULL; /* Prevent disconnect message */
	} else {
		reply (event, E_CREDENTIALS);
	}
	return;
}



#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
/* Process input from connection */
void execute_command (APPSTATE *app, FB_EVENT *event) {
	USER_CONTEXT *context = (USER_CONTEXT *)event->context;
	PianoStation_t *station;
	PianoSong_t *song;
	struct user_t *newuser;
	char *errorpoint;
	char *temp;
	DIR *dpath;
	int i;
	long l;
	COMMAND cmd = fb_interpret (app->parser, event->argv, &errorpoint);

	/* UNPRIVILEGED COMMANDS START HERE. */
	/* These guys aren't even authorized to get parser error messages. */
	switch (cmd) {
		case HELP:
			reply (event, S_OK);
			send_statement_list (event, context->user, event->argv[1]);
			return;
		case QUIT:
			reply (event, S_SIGNOFF);
			fb_close_connection (event->connection);
			return;
		case AUTHENTICATE:
			context->user = authenticate_user (event->argv[1], event->argv[2]);
			if (!context->user) {
				reply (event, E_CREDENTIALS);
				return;
			}
			announce_action (event, app, A_SIGNED_IN, NULL);
			if (app->current_song) {
				send_station_rating (event, app->current_song->stationId);
			}
			recompute_stations (app);
			/* FALLTHRU */
		case GETUSERRANK:
			reply (event, S_OK);
			send_privileges(event, context->user);
			return;
		case AUTHANDEXEC:
			authorize_and_execute (app, event);
			return;
	}

	/* Check for listen-only or better rank */
	if (!have_rank (context->user, RANK_LISTENER)) {
		reply (event, E_UNAUTHORIZED);
		return;
	}


	/* OWNER PRIVILEGE COMMANDS START HERE. */
	/* Owners can change the Pandora stations, add/remove seeds, rate songs, etc. */
	if (cmd > OWNER_RANGE_START && cmd < OWNER_RANGE_END) {
		if (!have_privilege (context->user, PRIVILEGE_MANAGER)) {
			reply (event, E_UNAUTHORIZED);
			return;
		}
		switch (cmd) {
			case STATIONINFO:
				if ((station = get_station_by_name_or_current(app, event, event->argv[2]))) {
					send_station_info (app, event, station);
				}
				return;
			case STATIONRENAME:
				rename_station (app, event, event->argv[2], event->argv[4]);
				return;
			case STATIONDELETE:
				if ((station = PianoFindStationByName(app->ph.stations, event->argv[2]))) {
					if (app->selected_station == station) {
						app->selected_station = NULL;
					}
					if (piano_transaction (app, event, PIANO_REQUEST_DELETE_STATION, station)) {
						send_response (event->service, I_STATIONS_CHANGED);
						announce_action (event, app, A_DELETED_STATION, event->argv[2]);
					};
				} else {
					reply (event, E_NOTFOUND);
				}
				return;
			case STATIONCREATEBYSONG:
				create_station_from_song (app, event, NULL,
										  strcasecmp (event->argv[3], "artist") == 0,
										  event->argv [4]);
				return;
			case STATIONCREATEBYSONGWNAME:
				create_station_from_song (app, event, event->argv[3],
										  strcasecmp (event->argv[3], "artist") == 0,
										  event->argv [6]);
				return;
			case STATIONCREATEBYID:
				create_station_from_shared (app, event, NULL, event->argv [4]);
				return;
			case STATIONCREATEBYIDWNAME:
				create_station_from_shared (app, event, event->argv [3], event->argv [6]);
				return;
			case STATIONCREATE:
				create_station_from_suggestion (app, event, NULL, event->argv [4]);
				return;
			case STATIONCREATEWNAME:
				create_station_from_suggestion (app, event, event->argv [3], event->argv [6]);
				return;
			case GETSUGGESTIONS:
				perform_query (app, event, event->argv [2]);
				return;
			case RATELOVE:
			case RATEHATE:
			case RATENEUTRAL:
			case RATEOVERPLAYED:
				rate_song (app, event, cmd, event->argv[2]);
				return;
			case SEEDADD:
				add_seed (app, event, NULL, event->argv[4]);
				return;
			case SEEDADDWSTATION:
				add_seed (app, event, event->argv[6], event->argv[4]);
				return;
			case SEEDDELETEBYSONG:
				remove_song_seed (app, event, strcasecmp (event->argv[1], "artist") == 0, event->argv[3]);
				return;
			case SEEDDELETEBYID:
				remove_seed (app, event, event->argv[2]);
				return;
			case SEEDADDBYSONG:
				add_song_seed (app, event, NULL,
									strcasecmp (event->argv[1], "artist") == 0,
									event->argv [5]);
				return;
			case SEEDADDBYSONGWSTATION:
				add_song_seed (app, event, event->argv[4],
									strcasecmp (event->argv[1], "artist") == 0,
									event->argv [7]);
				return;
			case EXPLAINSONGCHOICE:
				explain_song_choice (app, event, event->argv [2]);
				return;
			case CREATEBOOKMARK:
				if ((song = get_song_by_id_or_current(app, event, event->argv [4]))) {
					piano_transaction(app, event, (strcasecmp (event->argv [1], "artist") == 0) ?
												  PIANO_REQUEST_BOOKMARK_ARTIST :
												  PIANO_REQUEST_BOOKMARK_SONG, song);
				}
				return;
		}
		reply (event, E_NOT_IMPLEMENTED);
		return;
	}


	/* LISTENER COMMANDS START HERE. */
	/* Listeners can view status, but not change anything */
	switch (cmd) {
		/* Parser Errors */
		case FB_PARSE_FAILURE:
			reply (event, E_FAILURE);
			return;
		case FB_PARSE_INCOMPLETE:
			fb_fprintf (event, "%03d Command incomplete after %s\n", E_BAD_COMMAND, errorpoint);
			return;
		case FB_PARSE_INVALID_KEYWORD:
			fb_fprintf (event, "%03d Bad command %s\n", E_BAD_COMMAND, errorpoint);
			return;
		case FB_PARSE_NUMERIC:
			fb_fprintf (event, "%03d Numeric value expected: %s\n", E_BAD_COMMAND, errorpoint);
			return;
        case FB_PARSE_RANGE:
            fb_fprintf (event, "%03d Numeric value out of range: %s\n", E_BAD_COMMAND, errorpoint);
            return;
		case FB_PARSE_EXTRA_TERMS:
			fb_fprintf (event, "%03d Run-on command at %s\n", E_BAD_COMMAND, errorpoint);
			return;
		case NOP:
			return;
		/* Server functions */
		case YELL:
			announce_action (event, app, I_YELL, event->argv[1]);
			reply (event, S_OK);
			return;
		/* Get status of various settings */
		case GETVOLUME:
			reply (event, S_DATA);
			fb_fprintf (event, "%03d %s: %d\n", I_VOLUME, Response (I_VOLUME), app->settings.volume);
			reply (event, S_DATA_END);
			return;
		case AUTOTUNEGETMODE:
			report_setting (event, I_AUTOTUNE_MODE, app->settings.automatic_mode == TUNE_ON_LOGINS ? "login" :
					   app->settings.automatic_mode == TUNE_ON_ATTRIBUTE ? "flag" : "all");
			return;
		case GETHISTORYSIZE:
			reply (event, S_DATA);
			fb_fprintf (event, "%03d %s: %d\n", I_HISTORYSIZE, Response (I_HISTORYSIZE), app->settings.history_length);
			reply (event, S_DATA_END);
			return;
		case GETAUDIOQUALITY:
			temp = PianoGetAudioQualityName (app->settings.audioQuality);
			report_setting (event, I_AUDIOQUALITY, temp);
			return;
		/* Pandora commands */
		case TIMESTATUS:
			send_playback_status (event, app);
			return;
		case QUERYSTATUS:
			if (app->current_song) {
				reply (event, S_DATA);
				send_song_info (event, app, app->current_song);
				send_station_rating (event, app->current_song->stationId);
			}
			reply (event, S_DATA_END);
			send_playback_status (event, app);
			send_selectedstation (event, app);
			return;
		case QUERYHISTORY:
		case QUERYQUEUE:
			send_song_lists (app, event, cmd);
			return;
		case QUICKMIXINCLUDED:
		case QUICKMIXEXCLUDED:
		case STATIONLIST:
			send_station_list (event, app->ph.stations, cmd);
			return;
		case STATIONRATINGS:
			if (context->user) {
				send_station_ratings (app, event, context->user);
			} else {
				reply (event, E_LOGINREQUIRED);
			}
			return;
		case STATIONRATE:
			if (context->user) {
				rate_station (app, event, context->user);
			} else {
				reply (event, E_LOGINREQUIRED);
			}
			return;
		case SETMYPASSWORD:
			if (context->user) {
				reply (event, change_password (context->user, event->argv[2], event->argv[3]) ?
					   S_OK : E_CREDENTIALS);
			} else {
				reply (event, E_LOGINREQUIRED);
			}
			return;
		case WAITFORENDOFSONG:
			if (app->current_song) {
				wait_for_event (event, EVENT_TRACK_ENDED);
			} else {
				reply (event, E_WRONG_STATE);
			}
			return;
		case WAITFORNEXTSONG:
			wait_for_event (event, EVENT_TRACK_STARTED);
			return;

		/* Special privilege commands */
		case USERSONLINE:
			/* If sharing user actions, anyone can view users online (but not their privileges. */
			/* Admins can always see users online and get their privileges. */
			if (have_rank (context->user, RANK_ADMINISTRATOR) || app->settings.broadcast_user_actions) {
				send_select_users (app->service, event, SEND_USERS_ONLINE, have_rank (context->user, RANK_ADMINISTRATOR));
			} else {
				reply (event, E_UNAUTHORIZED);
			}
			return;
        case USERLISTPANDORA:
			if (have_privilege(context->user, PRIVILEGE_SERVICE)) {
                send_select_users(app->service, event, SEND_USERS_REMEMBERING_CREDENTIALS,
                                  have_rank(context->user, RANK_ADMINISTRATOR));
            } else {
                reply (event, E_UNAUTHORIZED);
            }
            return;
		case PANDORAUSER:
		case PANDORAUSERSPECIFY:
			/* "pandora user {user} {passwd} [mine|unowned]" */
			/* "pandora user {user} {passwd} owned by {user}" */
			if (have_privilege(context->user, PRIVILEGE_SERVICE)) {
                announce_action (event, app, A_PANDORA_SET, NULL);
				accept_new_credentials (app, event,
										cmd == PANDORAUSERSPECIFY ? event->argv [6] : NULL);
			} else {
				reply (event, E_UNAUTHORIZED);
			}
			return;
        case PANDORAEXISTING:
            if (!have_privilege (context->user, PRIVILEGE_SERVICE)) {
                reply (event, E_UNAUTHORIZED);
            } else if ((newuser = get_user_by_name(event, event->argv [2]))) {
                if (restore_pandora_credentials (newuser, &app->settings.pending)) {
                    set_pandora_user (app, event); /* function sends status */
					announce_action (event, app, context->user == newuser ? A_PANDORA_SET : A_PANDORA_BORROW,
                                     context->user == newuser ? NULL : get_user_name (newuser));
                } else {
                    reply (event, E_WRONG_STATE); /* User doesn't have saved creds */
                }
            };
            return;
		case WAITFORAUTHENTICATION:
			if (have_privilege (context->user, PRIVILEGE_SERVICE)) {
				if (app->settings.pending.username) {
					wait_for_event (event, EVENT_AUTHENTICATED);
				} else {
					reply (event, E_WRONG_STATE);
				}
			} else {
				reply (event, E_UNAUTHORIZED);
			}
			return;
        case AUTOTUNEUSERSLIST:
			if (have_privilege(context->user, PRIVILEGE_TUNER) ||
                app->settings.broadcast_user_actions) {
                send_select_users(app->service, event, app->settings.automatic_mode,
                                  have_rank(context->user, RANK_ADMINISTRATOR));
            } else {
                reply (event, E_UNAUTHORIZED);
            }
            return;
		case AUTOTUNEUSERS:
		case AUTOTUNEADDREMOVE:
			if (have_privilege(context->user, PRIVILEGE_TUNER)) {
				if (valid_user_list (event, event->argv + 2)) {
					if (cmd == AUTOTUNEUSERS) {
						clear_privilege (ATTRIBUTE_PRESENT);
					}
					set_privileges (event->argv + 2, ATTRIBUTE_PRESENT,
								    (strcasecmp (event->argv [1], "disregard") == 0) ? false : true);
					reply (event, S_OK);
					recompute_stations (app);
				}
			} else {
				reply (event, E_UNAUTHORIZED);
			}
			return;
	}


	/* Check for user or better rank */
	if (!have_rank (context->user, RANK_STANDARD)) {
		reply (event, E_UNAUTHORIZED);
		return;
	}

	/* USER COMMANDS START HERE */
	/* Users can control playback, but not modify stations */
	switch (cmd) {
		case SETVOLUME:
			/* Use a ±100 scale. 0 is "standard" level; >0 causes distortion. */
			errorpoint = NULL;
			if (strcasecmp (event->argv [1], "up") == 0) {
				if (app->settings.volume >= 100) {
					data_reply (event, E_INVALID, "Already at maximum volume");
					return;
				}
				app->settings.volume ++;
			} else if (strcasecmp (event->argv [1], "down") == 0) {
				if (app->settings.volume <= -100) {
					data_reply (event, E_INVALID, "Already at minimum volume");
					return;
				}
				app->settings.volume --;
			} else {
				l = strtol(event->argv [1], &errorpoint, 10);
				if (*errorpoint || l < -100 || l > 100) {
					reply (event, E_INVALID);
					return;
				}
				app->settings.volume = l;
			}
			reply (event, S_OK);
			app->player.scale = BarPlayerCalcScale (app->player.gain + app->settings.volume);
			fb_fprintf (app->service, "%03d %s: %d\n", I_VOLUME, Response (I_VOLUME), app->settings.volume);
			return;
		case NEXTSONG:
			if (app->player.mode > PLAYER_INITIALIZED) {
				if (app->player.doQuit || skips_are_available (app, event, app->current_song->stationId)) {
					cancel_playback (app);
					announce_action (event, app, A_SKIPPED, app->current_song->title);
					reply (event, S_OK);
				} else {
					reply (event, E_QUOTA);
				}
			} else {
				reply (event, E_WRONG_STATE);
			}
			return;
		case PLAYSTATION:
		case PLAYQUICKMIX:
		case SELECTSTATION:
		case SELECTQUICKMIX:
			/* Unlike "standard" Pandora, changing station won't be immediate.
			 Instead, play out the current song. If a skip is desired, user
			 they can command that subsequently.  The queue is left unaltered;
			 inappropriate songs will be skipped when track changes by
			 purge_unselected_songs(). */
			station = (cmd == PLAYQUICKMIX || cmd == SELECTQUICKMIX) ? PianoFindQuickMixStation (app->ph.stations) :
																	   PianoFindStationByName(app->ph.stations, event->argv[2]);
			if (station) {
				app->selected_station = station;
				app->automatic_stations = ((cmd == PLAYQUICKMIX || cmd == SELECTQUICKMIX) && strcasecmp (event->argv[1], "auto") == 0);
				announce_action (event, app, A_SELECTED_STATION, app->selected_station->name);
				send_selectedstation (app->service, app);
				recompute_stations (app);
				if (cmd == SELECTSTATION || cmd == SELECTQUICKMIX) {
					reply (event, S_OK);
				} else {
					control_playback (app, event, PLAY);
				}
			} else {
				reply (event, E_NOTFOUND);
			}
			return;
		case STOPPLAYBACK:
			app->selected_station = NULL;
			if (event->argc == 2 /* stop now */) {
				cancel_playback (app);
			}
			announce_action (event, app, A_STOPPED, NULL);
			send_selectedstation (app->service, app);
			reply (event, S_OK);
			return;
		case PLAY:
		case PAUSEPLAYBACK:
		case PLAYPAUSE:
			i = app->playback_state;
			control_playback (app, event, cmd);
			if (i != app->playback_state) {
				announce_action (event, app, app->playback_state == PLAYING ? A_RESUMED : A_PAUSED, NULL);
			}
			return;
		case QUICKMIXSET:
		case QUICKMIXADD:
		case QUICKMIXDROP:
		case QUICKMIXTOGGLE:
			if (validate_station_list(app, event, (event->argv)+2)) {
				reply (event, manipulate_quickmix (app, event, cmd, (event->argv)+2));
			} else {
				reply (event, E_NOTFOUND);
			}
			return;
	}

	/* Check for administrator rank. */
	if (!have_rank (context->user, RANK_ADMINISTRATOR)) {
		reply (event, E_UNAUTHORIZED);
		return;
	}

	/* ADMINISTRATOR COMMANDS START HERE */
	/* Administrators can modify stations, add/remove user accounts, etc. */
	switch (cmd) {
		/* pianod/Pandora preference settings */
		case AUTOTUNESETMODE:
			if (strcasecmp (event->argv [2], "login") == 0) {
				app->settings.automatic_mode = TUNE_ON_LOGINS;
			} else if (strcasecmp (event->argv [2], "flag") == 0) {
				app->settings.automatic_mode = TUNE_ON_ATTRIBUTE;
			} else {
				assert (strcasecmp (event->argv [2], "all") == 0);
				app->settings.automatic_mode = TUNE_ON_ALL;
			}
			reply (event, S_OK);
			recompute_stations (app);
			return;
		case SETHISTORYSIZE:
			i = atoi (event->argv [3]);
            app->settings.history_length = i;
            fb_fprintf (app->service, "%03d %s: %d\n", I_HISTORYSIZE, Response (I_HISTORYSIZE), i);
            reply (event, S_OK);
			return;
		case SETAUDIOQUALITY:
			if (strcasecmp (event->argv[3], "low") == 0) {
				app->settings.audioQuality = PIANO_AQ_LOW;
			} else if (strcasecmp (event->argv [3], "medium") == 0) {
				app->settings.audioQuality = PIANO_AQ_MEDIUM;
			} else {
				app->settings.audioQuality = PIANO_AQ_HIGH;
			}
			temp = PianoGetAudioQualityName (app->settings.audioQuality);
			fb_fprintf (app->service, "%03d %s: %s\n", I_AUDIOQUALITY, Response (I_AUDIOQUALITY), temp);
			reply (event, S_OK);
			return;

#if defined(ENABLE_SHOUT)
		case SETSHOUTCAST:
			if (strcasecmp(event->argv[2], "off") == 0) {
				if (app->shoutcast) {
					sc_close_service(app->shoutcast);
					app->shoutcast = NULL;
					send_data (app->service, I_SHOUTCAST, "disabled");
				}
			} else if (strcasecmp(event->argv[2], "on") == 0) {
				if (app->shoutcast == NULL) {
					app->shoutcast = sc_init_service();
					if (app->shoutcast) {
						send_data (app->service, I_SHOUTCAST, "enabled");
					} else {
						reply (event, E_FAILURE);
						return;
					}
				}
			} else {
				reply (event, E_INVALID);
				return;
			}

			reply (event, S_OK);
			return;
#endif

#if defined(ENABLE_CAPTURE)
		/* Stream capture */
		case GETCAPTUREPATH:
			report_setting (event, I_CAPTUREPATH,
					(app->settings.capture_pathlen) ? app->settings.capture_path : "capture off");
			return;

		case SETCAPTUREPATH:
			/* Check if we will terminate at end of song */
			if (strcasecmp(event->argv[2], "off") == 0) {
				app->settings.capture_pathlen = 0;
				if (app->settings.capture_path) {
					free (app->settings.capture_path);
					app->settings.capture_path = NULL;
				}
				reply (event, S_OK);
				return;
			}

			if (event->argv[3] && (temp = strdup(event->argv[3]))) {
				/* Validate path exists */
				dpath = opendir(temp);
				if (dpath) {
					/* Directory exists. */
					closedir(dpath);
					if (app->settings.capture_path) {
						free (app->settings.capture_path);
					}
					app->settings.capture_path = temp;
					app->settings.capture_pathlen = strlen(temp);
					send_data (app->service, I_CAPTUREPATH, app->settings.capture_path);
					reply (event, S_OK);
				} else {
					free (temp);
					reply (event, E_NOTFOUND);
				}
			} else {
				data_reply (event, E_NAK, strerror (errno));
			}
			return;
#endif
		/* Libpiano settings */
		case GETCONTROLPROXY:
			report_setting (event, I_CONTROLPROXY, app->settings.control_proxy);
			return;
		case SETCONTROLPROXY:
			;
			if ((temp = strdup (event->argv [3]))) {
				if (WaitressSetProxy (&app->waith, temp)) {
					free (app->settings.control_proxy);
					app->settings.control_proxy = temp;
					send_data (app->service, I_CONTROLPROXY, app->settings.control_proxy);
					reply (event, S_OK);
				} else {
					reply (event, E_INVALID);
					free (temp);
				}
			} else {
				data_reply (event, E_NAK, strerror (errno));
			}
			return;
		case GETPAUSETIMEOUT:
			reply (event, S_DATA);
			fb_fprintf (event, "%03d %s: %d\n", I_PAUSE_TIMEOUT, Response (I_PAUSE_TIMEOUT), app->settings.pause_timeout);
			reply (event, S_DATA_END);
			return;
		case SETPAUSETIMEOUT:
			i = atoi (event->argv [3]);
            app->settings.pause_timeout = i;
            fb_fprintf (app->service, "%03d %s: %d\n", I_PAUSE_TIMEOUT, Response (I_PAUSE_TIMEOUT), i);
            reply (event, S_OK);
			return;
		case GETPLAYLISTTIMEOUT:
			reply (event, S_DATA);
			fb_fprintf (event, "%03d %s: %d\n", I_PLAYLIST_TIMEOUT, Response (I_PLAYLIST_TIMEOUT), app->settings.playlist_expiration);
			reply (event, S_DATA_END);
			return;
		case SETPLAYLISTTIMEOUT:
			i = atoi (event->argv [3]);
            app->settings.playlist_expiration = i;
            fb_fprintf (app->service, "%03d %s: %d\n", I_PLAYLIST_TIMEOUT, Response (I_PLAYLIST_TIMEOUT), i);
            reply (event, S_OK);
			return;
		case GETPANDORARETRY:
			reply (event, S_DATA);
			fb_fprintf (event, "%03d %s: %d\n", I_PANDORA_RETRY, Response (I_PANDORA_RETRY), app->settings.pandora_retry);
			reply (event, S_DATA_END);
			return;
		case SETPANDORARETRY:
			i = atoi (event->argv [3]);
			app->settings.pandora_retry = i;
			fb_fprintf (app->service, "%03d %s: %d\n", I_PANDORA_RETRY, Response (I_PANDORA_RETRY), i);
			reply (event, S_OK);
			return;
		case GETPROXY:
			report_setting (event, I_PROXY, app->settings.proxy);
			return;
		case SETPROXY:
			if (strncasecmp (event->argv [2], "http://", 7) != 0) {
				reply (event, E_INVALID);
			} else if ((temp = strdup (event->argv [2]))) {
				free (app->settings.proxy);
				app->settings.proxy = temp;
				if (!app->settings.control_proxy) {
					WaitressSetProxy (&app->waith, temp);
				}
				fb_fprintf (app->service, "%03d %s: %s\n", I_PROXY, Response (I_PROXY), app->settings.proxy);
				reply (event, S_OK);
			} else {
				data_reply (event, E_NAK, strerror (errno));
			}
			return;
		case GETRPCHOST:
			report_setting (event, I_RPCHOST, app->settings.rpcHost);
			return;
		case SETRPCHOST:
			change_setting (app, event, event->argv [3], &(app->settings.rpcHost));
			return;
		case GETRPCTLSPORT:
			report_setting (event, I_RPCTLSPORT, app->settings.rpcTlsPort);
			return;
		case SETRPCTLSPORT:
			change_setting (app, event, event->argv [4], &(app->settings.rpcTlsPort));
			return;
		case GETPANDORADEVICE:
			report_setting (event, I_PANDORADEVICE, app->settings.device);
			return;
		case SETPANDORADEVICE:
			change_setting (app, event, event->argv [3], &(app->settings.device));
			return;
		case GETENCRYPTPASSWORD:
			report_setting (event, I_ENCRYPTION_PASSWORD, app->settings.outkey);
			return;
		case SETENCRYPTPASSWORD:
			change_setting (app, event, event->argv [3], &(app->settings.outkey));
			return;
		case GETDECRYPTPASSWORD:
			report_setting (event, I_DECRYPTION_PASSWORD, app->settings.inkey);
			return;
		case SETDECRYPTPASSWORD:
			change_setting (app, event, event->argv [3], &(app->settings.inkey));
			return;
		case GETPARTNER:
			reply (event, S_DATA);
			data_reply (event, I_PARTNERUSER, app->settings.partnerUser);
			data_reply (event, I_PARTNERPASSWORD, app->settings.partnerPassword);
			reply (event, S_DATA_END);
			return;
		case SETPARTNER:
			if ((temp = strdup (event->argv [2]))) {
				if (change_setting (app, event, event->argv [3], &(app->settings.partnerPassword))) {
					free (app->settings.partnerUser);
					app->settings.partnerUser = temp;
					app->pianoparam_change_pending = true;
				} else {
					free (temp);
				}
			} else {
				reply (event, E_NAK);
			}
			return;
		case GETTLSFINGERPRINT:
			report_fingerprint (event, app->settings.tlsFingerprint);
			return;
		case SETTLSFINGERPRINT:
			change_fingerprint (app, event, event->argv [3]);
			return;

		/* Libao settings */
		case GETOUTPUTDRIVER:
			report_setting (event, I_OUTPUT_DRIVER, app->settings.output_driver);
			return;
		case SETOUTPUTDRIVER:
			if (event->argv [4] && ao_driver_id (event->argv [4]) < 0) {
				reply (event, E_NOTFOUND);
			} else {
				change_setting (app, event, event->argv [4], &(app->settings.output_driver));
			}
			return;
		case GETOUTPUTDEVICE:
			report_setting (event, I_OUTPUT_DEVICE, app->settings.output_device);
			return;
		case SETOUTPUTDEVICE:
			change_setting (app, event, event->argv [4], &(app->settings.output_device));
			return;
		case GETOUTPUTID:
			report_setting (event, I_OUTPUT_ID, app->settings.output_id);
			return;
		case SETOUTPUTID:
			change_setting (app, event, event->argv [4], &(app->settings.output_id));
			return;
		case GETOUTPUTSERVER:
			report_setting (event, I_OUTPUT_SERVER, app->settings.output_server);
			return;
		case SETOUTPUTSERVER:
			change_setting (app, event, event->argv [4], &(app->settings.output_server));
			return;
		case TESTAUDIOOUTPUT:
			if (app->current_song) {
				reply (event, E_WRONG_STATE);
			} else {
				generate_test_tone (app, event);
			}
			return;

		/* Other stuff */
		case SETLOGGINGFLAGS:
            if (event->argc == 4) {
				set_logging (strtol (event->argv [3], NULL, 0));
            } else {
				fb_set_logging (strtol (event->argv [4], NULL, 0), NULL);
            }
			return;
		case SHOWUSERACTIONS:
			app->settings.broadcast_user_actions = (strcasecmp (event->argv [3], "on") == 0);
			reply (event, S_OK);
			return;
		case SHUTDOWN:
			/* Commence a server shutdown, which will take effect after the current song. */
			app->quit_requested = true;
			reply (event, S_OK);
			return;
		case GETVISITORRANK:
			send_privileges(event, NULL);
			reply (event, S_OK);
			return;
		case SETVISITORRANK:
			set_visitor_rank (get_rank_by_name (event->argv [3]));
			announce_privileges (app->service, NULL);
			reply (event, S_OK);
			return;
		case GETPANDORAUSER:
			if (!app->settings.pandora.username) {
				data_reply (event, S_DATA_END, "Pandora credentials not set.");
			} else if (have_privilege (context->user, PRIVILEGE_MANAGER)) {
				reply (event, S_DATA);
				data_reply (event, I_PANDORA_USER, app->settings.pandora.username);
				data_reply (event, I_PANDORA_PASSWORD, app->settings.pandora.password);
				reply (event, S_DATA_END);
			} else {
				reply (event, E_UNAUTHORIZED);
			}
			return;
		case USERCREATE:
			/* Create a new user or administrator account for pianod */
			newuser = create_new_user (event->argv [2], event->argv [3]);
			if (newuser) {
				set_rank (newuser, get_rank_by_name (event->argv [1]));
				reply (event, S_OK);
			} else {
				reply (event, E_DUPLICATE);
			}
			return;
		case USERSETPASSWORD:
			newuser = get_user_by_name (event, event->argv [3]);
			if (newuser) {
				reply (event, set_user_password (newuser, event->argv [4]) ? S_OK : E_NAK);
			}
			return;
		case USERSETRANK:
			newuser = get_user_by_name (event, event->argv [3]);
			if (newuser) {
				set_rank (newuser, get_rank_by_name (event->argv [4]));
				announce_privileges (app->service, newuser);
				reply (event, S_OK);
			}
			return;
		case USERGRANT:
		case USERREVOKE:
			if (valid_user_list (event, event->argv + 3)) {
				PRIVILEGE privilege = get_privilege_id_by_name(event->argv [1]);
				set_privileges (event->argv + 3, privilege, cmd == USERGRANT);
				if (privilege == PRIVILEGE_INFLUENCE) {
					recompute_stations (app);
				}
				announce_privileges (app->service, NULL);
				reply (event, S_OK);
			}
			return;
        case USERLISTBYPRIVILEGE:
            send_select_users(app->service, event, get_privilege_id_by_name (event->argv [2]), true);
            return;
		case USERKICKVISITORS:
			user_logoff (app->service, NULL, event->argv[2]);
			reply (event, S_OK);
			return;
		case USERKICK:
			newuser = get_user_by_name (event, event->argv [2]);
			if (newuser) {
				if (is_user_online (app->service, newuser)) {
					user_logoff (app->service, newuser, event->argv [3]);
					reply (event, S_OK);
					announce_action (event, app, A_KICKED, event->argv[2]);
				} else {
					data_reply (event, E_WRONG_STATE, "User is not logged in.");
				}
			}
			return;
		case USERDELETE:
			newuser = get_user_by_name (event, event->argv [2]);
			if (newuser) {
				if (is_user_online (app->service, newuser)) {
					data_reply (event, E_WRONG_STATE, "User is logged in.");
				} else {
                    /* Detach any active/pending Pandora account ownership */
                    if (app->settings.pending.creator == newuser) {
                        app->settings.pending.creator = NULL;
                    }
                    if (app->settings.pending.manager == newuser) {
                        app->settings.pending.manager = NULL;
                    }
                    if (app->settings.pandora.creator == newuser) {
                        app->settings.pandora.creator = NULL;
                    }
                    if (app->settings.pandora.manager == newuser) {
                        app->settings.pandora.manager = NULL;
                    }
					delete_user (newuser);
					reply (event, S_OK);
				}
			}
			return;
		case USERLIST:
			send_user_list (event, event->argv [2]);
			return;
	}
	reply (event, E_NOT_IMPLEMENTED);
}
#pragma GCC diagnostic pop



/* Create a parser and add each group (one for each authorization level)
   of statement definitions to it.  All statements go in the same parser;
   authorization is handled by execute_command by checks between the
   switch statements. */
bool init_parser (APPSTATE *app) {
	if ((app->parser = fb_create_parser())) {
		bool ok = true;
		size_t i;
		for (i = 0; i < countof (allstatements); i++) {
			ok = fb_parser_add_statements (app->parser, allstatements [i].commandset,
										   allstatements [i].count) && ok;
		}
		if (ok) {
			return true;
		}
		fb_parser_destroy (app->parser);
		app->parser = NULL;
	} else {
		flog (LOG_ERROR, "Couldn't create parser.");
	}
	return false;
}
