/*
 *  service.c
 *  Server-style UI for libpiano.
 *
 *  Created by Perette Barella on 2012-03-10.
 *  Copyright 2012 Devious Fish. All rights reserved.
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


#ifndef __FreeBSD__
#define _POSIX_C_SOURCE 1 /* fileno() */
#define _BSD_SOURCE /* strdup() */
#define _DARWIN_C_SOURCE /* strdup() on OS X */
#endif

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include <getopt.h>
#include <limits.h>
#include <dirent.h>

#include <fb_public.h>
#include <piano.h>

#include "command.h"
#include "support.h"
#include "pianoextra.h"
#include "response.h"
#include "pianod.h"
#include "logging.h"
#include "seeds.h"
#include "users.h"
#include "query.h"
#include "tuner.h"

#if defined(ENABLE_CAPTURE)
extern void ripit_open_file(struct audioPlayer *player, PianoSong_t *song);
#endif

#ifndef HAVE_SETPROGNAME
static const char *progname;
void setprogname (const char *name) {
    const char *last = strrchr(name, '/');
    if (last) {
        progname = last+1;
    } else {
        progname = name;
    }
}
const char *getprogname (void) {
    return progname;
}
#endif

/*	Fetch a new playlist from Pandora.
 */
static void get_play_list (APPSTATE *app) {
	assert (app->playlist == NULL);

	PianoRequestDataGetPlaylist_t reqData;
	memset (&reqData, 0, sizeof (reqData));
	reqData.station = app->selected_station;
	reqData.quality = app->settings.audioQuality;

	
	flog (LOG_GENERAL, "Retrieving new playlist");
	if (!piano_transaction (app, NULL, PIANO_REQUEST_GET_PLAYLIST, &reqData)) {
		app->selected_station = NULL;
		send_selectedstation (app->service, app);
	} else {
		app->playlist = reqData.retPlaylist;
		if (app->playlist == NULL) {
			send_response_code (app->service, E_RESOURCE, "Unable to retrieve playlist");
			app->selected_station = NULL;
			send_selectedstation (app->service, app);
		} else {
			send_status (app->service, "Retrieved new playlist");
			app->playlist_retrieved = time (NULL);
		}
	}
}

/*	start new player thread
 *  Preconditons: playlist should have a list in it.
 */
#define strdup_nullable(x) ((x) ? strdup (x) : NULL)
static void playback_start (APPSTATE *app, pthread_t *playerThread) {
	/* Get a song off the playlist */
	assert (!app->current_song);
	assert (app->playlist);
	
	app->current_song = app->playlist;
	app->playlist = PianoListNextP (app->playlist);
	app->current_song->head.next = NULL;
	
	/* Now play it */
	if (app->current_song->audioUrl == NULL) {
		send_response_code (app->service, E_FAILURE, "Invalid song url.");
		PianoDestroyPlaylist (app->current_song);
		app->current_song = NULL;
		/* We'll try again on the next iteration */
	} else {
		/* setup player */
		memset (&app->player, 0, sizeof (app->player));
		memset (&app->stall, 0, sizeof (app->stall));
		
		WaitressInit (&app->player.waith);
		WaitressSetUrl (&app->player.waith, app->current_song->audioUrl);
		
		/* set up global proxy, player is NULLed on songfinish */
		if (app->settings.proxy != NULL) {
			if (!WaitressSetProxy (&app->player.waith, app->settings.proxy)) {
				send_response (app->service, I_PROXY_CONFIG);
			}
		}
		
		app->player.gain = app->current_song->fileGain;
		app->player.scale = BarPlayerCalcScale (app->player.gain + app->settings.volume);
		app->player.audioFormat = app->current_song->audioFormat;
		app->player.settings = &app->settings;
		app->player.driver = strdup_nullable (app->settings.output_driver);
		app->player.device = strdup_nullable (app->settings.output_device);
		app->player.id = strdup_nullable (app->settings.output_id);
		app->player.server = strdup_nullable (app->settings.output_server);
#if defined(ENABLE_SHOUT)
		app->player.shoutcast = app->shoutcast;
#endif
		/* Create the mutex and condition variable before we create the thread */
		int err;
		if ((err = pthread_mutex_init (&app->player.pauseMutex, NULL)) != 0) {
			flog (LOG_ERROR, "pthread_mutex_init: %s", strerror (err));
		}
		if ((err = pthread_cond_init (&app->player.pauseCond, NULL)) != 0) {
			flog (LOG_ERROR, "pthread_cond_init: %s", strerror (err));
		}

		/* prevent race condition, mode must _not_ be FREED if
		 * thread has been started */
		app->player.mode = PLAYER_STARTING;

#if defined(ENABLE_CAPTURE)
		/* open stream capture file if path given */
		if (app->settings.capture_pathlen > 0) {
			ripit_open_file(&app->player, app->current_song);
		}
#endif

#if defined(ENABLE_SHOUT)
		/* Setup stream metadata */
		if (app->shoutcast) {
			sc_set_metadata(app->shoutcast, app->current_song);
		}
#endif
		/* start player */
		pthread_create (playerThread, NULL, BarPlayerThread,
						&app->player);

		/* The duration isn't known until the player initializes. Flag it as a to-do. */
		app->broadcast_status = true;
	}
}

/*	Player thread has completed, clean up.
 */
static void playback_cleanup (APPSTATE *app, pthread_t *playerThread) {
	assert (app->current_song);
	
	void *threadRet;
	pthread_join (*playerThread, &threadRet);

	/* Destroy the mutex and condition variable since we don't need it anymore */
	pthread_cond_destroy (&app->player.pauseCond);
	pthread_mutex_destroy (&app->player.pauseMutex);

	send_response (app->service, I_TRACK_COMPLETE);
	/* If the player thread reports an error, stop if it's a hard error or */
	/* there are multiple sequential soft errors.  On a single soft error, */
	/* keep going and try again. */
	if (threadRet != (void *) PLAYER_RET_OK) {
		bool soft = (threadRet == (void *) PLAYER_RET_SOFTFAIL);
		send_data (app->service, E_FAILURE,
				    soft ? "Transient player error" : "Player failure");
		if (soft) {
			app->player_soft_errors++;
		}
		/* Hard error or multiple sequential soft errors, stop. */
		/* A single soft error, keep going. */
		if (app->selected_station && (!soft || app->player_soft_errors > 1)) {
			app->selected_station = NULL;
			send_selectedstation (app->service, app);
		}
	} else {
		app->player_soft_errors = 0;
	}

	/* Report time for any stall before player exited */
	if (app->stall.stalled) {
		flog (LOG_WARNING, "Playback stalled for %d seconds", (int) (time (NULL) - app->stall.since));
	}
	
	free (app->player.id);
	free (app->player.server);
	free (app->player.device);
	free (app->player.driver);

	memset (&app->player, 0, sizeof (app->player));
	memset (&app->stall, 0, sizeof (app->stall));
	
	/* Move the completed song into the history. */
	prepend_history (app, app->current_song);
	app->current_song = NULL;
	
	event_occurred (app->service, EVENT_TRACK_ENDED, S_OK);
}



/* Process events from libfootball */
static bool run_service (APPSTATE *app) {
	/* It would be lovely to determine the time remaining and timeout with that.
	   Except then if the connection breaks and player stops, we have dead air for the
	   duration of where the song should be.  So timeout every few seconds, so we
	   recover tolerably. */
	FB_EVENT *event = fb_poll_with_timeout (1.0);

	assert (event);
	if (event == NULL) {
		flog (LOG_ERROR, "fb_poll_with_timeout: Null response (failure)");
		return false;
	}
	USER_CONTEXT *context = (USER_CONTEXT *)event->context;

	switch (event->type) {
		case FB_EVENT_CONNECT:
			/* Greet the connection, allocate any resources */
			flog (LOG_EVENT, "%-5d: New connection", event->socket);
			fb_fprintf (event->connection, "%03d Connected\n", S_OK);
			reply (event, I_WELCOME);
			fb_fprintf (event, "%03d %s: %d\n", I_VOLUME, Response (I_VOLUME), app->settings.volume);
			send_selectedstation (event, app);
			send_playback_status (event, app);
			if (app->current_song) {
				send_song_info (event, app, app->current_song);
				send_station_rating (event, app->current_song->stationId);
			}
			break;
		case FB_EVENT_CLOSE:
			/* Free user context resources */
			if (context->user) {
				announce_action (event, app, A_SIGNED_OUT, NULL);
			}
			destroy_search_context ((USER_CONTEXT *) event->context);
			context->user = NULL;
			recompute_stations (app);
			flog (LOG_EVENT, "%-5d: Connection closed", event->socket);
			break;
		case FB_EVENT_INPUT:
			flog (LOG_EVENT | LOG_COMMAND, "%-5d: Command: \"%s\"", event->socket, event->command);
			execute_command (app, event);
			break;
		case FB_EVENT_STOPPED:
			flog (LOG_EVENT, "Service is shutting down.");
			app->service = NULL;
			break;
		case FB_EVENT_TIMEOUT:
			/* Handle whatever timeout we asked for */
			flog (LOG_EVENT, "       Timeout has fired");
			break;
		case FB_EVENT_WRITABLE:
			/* Handle writable condition on user stream */
			flog (LOG_EVENT, "%-5d: Stream is ready for writing", event->socket);
			assert (0);
			break;
		case FB_EVENT_READABLE:
			/* Handle input on user stream */
			flog (LOG_EVENT, "%-5d: Stream has input ready", event->socket);
			assert (0);
			break;
		case FB_EVENT_FAULTING:
			/* Handle input on user stream */
			flog (LOG_EVENT, "%-5d: Stream is registering an error", event->socket);
			assert (0);
			break;
		default:
			flog (LOG_EVENT, "%-5d: Unknown event type %d received", event->socket, event->type);
			assert (0);
			break;
	}
	return true;
}



/* Change the settings on libpiano.  This involves uninitializing/reinitializng
   it with the new settings. */
static void change_piano_settings (APPSTATE *app) {
	PianoHandle_t new_ph;
	PianoReturn_t status = PianoInit (&new_ph, app->settings.partnerUser, app->settings.partnerPassword,
			   app->settings.device, app->settings.inkey, app->settings.outkey);
	if (status == PIANO_RET_OK) {
		/* Station list is inside libpiano, and selected station is a pointer to it */
		if (app->selected_station) {
			app->selected_station = NULL;
			send_selectedstation (app->service, app);
		}
		PianoDestroy (&app->ph);
		app->ph = new_ph;
	} else {
		send_response_code (app->service, E_INCOMPLETE, PianoErrorToStr (status));
		flog (LOG_ERROR, "change_piano_settings: PianoInit: %s", PianoErrorToStr (status));
		flog (LOG_WARNING, "change_piano_settings: Unable to fully update library settings.");
	}
		
	/* Waitress doesn't need to be fully reinitialized */
	app->waith.url.host = app->settings.rpcHost;
	app->waith.url.tlsPort = app->settings.rpcTlsPort;
	app->waith.tlsFingerprint = (const char *) app->settings.tlsFingerprint;
	/* Rewind the state */
	if (app->settings.pending.username) {
		destroy_pandora_credentials (&app->settings.pandora);
	} else {
		app->settings.pending = app->settings.pandora;
		memset (&app->settings.pandora, 0, sizeof (app->settings.pandora));
	}
	app->retry_login_time = app->settings.pending.username ? 1 /* Now */ : 0;
	app->update_station_list = 0;
}



/* Check/respond to various things with the player */
static void check_player_status (APPSTATE *app) {
	time_t now = time (NULL);
	if (app->broadcast_status) {
		/* Broadcast status after the start of the song.  Each of these would
		 better fit elsewhere, but for various reasons can't be there. */
		/* Duration isn't known in playback_start, so send it out here. */
		send_playback_status (app->service, app);
		/* Song info is broadcast at start of the song, but seed data is updated
         afterward to maximize responsiveness.  Rebroadcast the ratings in
         case they changed. */
        send_song_rating (app->service, app->current_song);
		app->broadcast_status = false;
		event_occurred(app->service, EVENT_TRACK_STARTED, S_OK);
		/* This seems like a good place to periodically persist the user data */
		users_persist (app->settings.user_file);
	} else if (app->playback_state == PLAYING) {
		signed long song_remaining = (signed long int) (app->player.songDuration -
											   app->player.songPlayed) / BAR_PLAYER_MS_TO_S_FACTOR;
		if (app->selected_station && song_remaining <= 5) {
			purge_unselected_songs(app);
		}
		if (app->selected_station && app->playlist == NULL && !app->pianoparam_change_pending) {
			/* Anticipate when we're within seconds of completing the playlist,
			 and gather a new one just-in-time to minimize breaks in playback. */
			/* Ugly: songDuration is unsigned _long_ int! Lets hope this won't overflow */
			if (song_remaining < 15) { /* Less than 15 seconds of playback left */
				update_station_list(app);
				/* If the current station still exists, use it. */
				if (app->selected_station) {
					get_play_list (app);
					apply_station_info (app);
				}
			}
		}
		/* Check for/announce/track stalls */
		bool stalled = false;
		if (app->stall.sample_time && song_remaining == app->stall.sample) {
			/* There is a previous sample and it hasn't changed */
			stalled = (now - app->stall.sample_time > 2);
		} else {
			/* Either there's no previous sample, or it's changing (we're not stalled) */
			app->stall.sample_time = now;
			app->stall.sample = song_remaining;
		}
		if (stalled && !app->stall.stalled) {
			/* New stall detected. */
			app->stall.since = app->stall.sample_time;
		} else if (!stalled && app->stall.stalled) {
			/* Playback has resumed. */
			flog (LOG_WARNING, "Playback stalled for %d seconds", (int) (now - app->stall.since));
		}
		if (app->stall.stalled != stalled) {
			app->stall.stalled = stalled;
			send_playback_status (app->service, app);
		}
	} else if (app->playback_state == PAUSED && app->paused_since) {
		/* If we're paused too long, we lose the connection to Pandora so
		 on resuming, we play out the buffer then crap out.  Instead,
		 if we're paused too long, just cancel playback. */
		time_t paused_duration = now - app->paused_since;
		if (paused_duration > app->settings.pause_timeout) {
			cancel_playback (app);
		}
	}
}

/* Shutdown signal handler, which sets global variable to be read
   by the main run loop. (Had OS X issues with these being static...) */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
bool shutdown_signalled = false;
void receive_signal (int signum) {
	shutdown_signalled = 1;
	signal (signum, SIG_IGN);
}
#pragma GCC diagnostic pop

/*	Main loop.
 */
static void pianod_run_loop (APPSTATE *app) {
	pthread_t playerThread;

	/* little hack, needed to signal: hey! we need a playlist, but don't
	 * free anything (there is nothing to be freed yet) */
	memset (&app->player, 0, sizeof (app->player));
	
	while (app->service) {
		/* If song finished playing, clean up things */
		if (app->player.mode == PLAYER_FINISHED_PLAYBACK) {
			playback_cleanup (app, &playerThread);
		}
		
		/* If requested, change the connection parameters between songs. */
		if (app->player.mode == PLAYER_FREED && app->pianoparam_change_pending) {
			app->pianoparam_change_pending = false;
			change_piano_settings (app);
		}

		/* If the user/station data hasn't been initialized, do so if we can */
		if (app->retry_login_time) {
			/* We haven't successfully logged in yet.  Try again periodically. */
			if (app->retry_login_time < time(NULL)) {
				set_pandora_user (app, NULL);
			}
		}

		/* If player is fresh and ready to go, start it up */
		if (app->player.mode == PLAYER_FREED) {
			if (app->quit_requested) {
				/* Handle pianod shutdown at end of song */
				if (!app->quit_initiated) {
					send_response (app->service, E_SHUTDOWN);
					fb_close_service (app->service);
					app->quit_initiated = 1;
				}
			} else if (app->selected_station == NULL) {
				/* There's no station, so put the player into paused state
				   if it's not already there. */
				if (app->playback_state != PAUSED) {
					app->playback_state = PAUSED;
					send_playback_status (app->service, app);
				}
			} else if (app->automatic_stations && computed_stations_is_empty_set()) {
				/* Don't do anything because automatic station selection says the
				   current listeners can't agree on the music. */
			} else if (app->playback_state == PLAYING) {
				/* what's next? */
				purge_unselected_songs(app);
				/* If we need a new playlist, get one */
				bool new_list = (app->playlist == NULL);
				if (new_list) {
					update_station_list(app);
					/* If the current station still exists, use it. */
					if (app->selected_station) {
						get_play_list (app);
					}
				}
				/* song ready to play */
				if (app->playlist != NULL) {
#if defined(ENABLE_SHOUT)
					// Startup shoutcast service or terminate it if not supported
					if (app->shoutcast) {
						// Check playlist for stream format
						if (app->playlist->audioFormat == PIANO_AF_AACPLUS) {
							flog(LOG_ERROR, "shout: AAC not supported by shoutcast");
							sc_close_service(app->shoutcast);
							app->shoutcast = NULL;
						} else {
							// MP3 OK (192Kb assumed)
							if (sc_start_service(app->shoutcast)) {
								flog(LOG_ERROR, "Shoutcast startup failed");
								sc_close_service(app->shoutcast);
								app->shoutcast = NULL;
							}
						}
					}
#endif
					playback_start (app, &playerThread);
					send_song_info (app->service, app, app->current_song);
					announce_station_ratings (app, NULL);
					/* If we got a new list, fill in metadata now.  This would be better
					   as part of get_play_list, but in the interests of responsiveness... */
					if (new_list) {
						apply_station_info (app);
					}
				}
			}
		}
		/* If the playback thread is valid, do various monitoring/handling on it */
		if (app->player.mode >= PLAYER_SAMPLESIZE_INITIALIZED &&
			app->player.mode < PLAYER_FINISHED_PLAYBACK) {
			check_player_status (app);
		}
		if (app->playlist) {
			time_t now = time (NULL);
			if (now > app->playlist_retrieved + app->settings.playlist_expiration) {
				/* The playlist has expired; attempting to play items from it will fail. */
				PianoDestroyPlaylist(app->playlist);
				app->playlist = NULL;
			}
		}
		
		run_service (app); /* See if sockets need attention */
		
		/* Check the signal handler's flag for shutdown requests. */ 
		if (shutdown_signalled) {
			shutdown_signalled = false;
			app->quit_requested = true;
			cancel_playback (app);
		}
	}
	
	if (app->player.mode != PLAYER_FREED) {
		/* Cancel may prevent full clean-up, but we're shutting down anyway. */
		/* Avoids hang if the player thread is stuck in network I/O */
		pthread_cancel (playerThread);
		pthread_join (playerThread, NULL);
	}
}


/* Create the initial libpiano and libwaitress instances */
static bool initialize_libraries (APPSTATE *app) {
	/* init some things */
	gcry_check_version (NULL);
	gcry_control (GCRYCTL_DISABLE_SECMEM, 0);
	gcry_control (GCRYCTL_INITIALIZATION_FINISHED, 0);

#if !defined(USE_POLARSSL)
	int crystatus = gnutls_global_init ();
	if (crystatus == GNUTLS_E_SUCCESS) {
#endif
		PianoReturn_t status = PianoInit (&app->ph, app->settings.partnerUser, app->settings.partnerPassword,
										  app->settings.device, app->settings.inkey, app->settings.outkey);
		if (status == PIANO_RET_OK) {
			WaitressInit (&app->waith);
			app->waith.url.host = app->settings.rpcHost;
			app->waith.url.tlsPort = app->settings.rpcTlsPort;
			app->waith.tlsFingerprint = (const char *) app->settings.tlsFingerprint;
			ao_initialize ();
			return true;
		} else {
			flog (LOG_ERROR, "initialize_libraries: PianoInit: %s", PianoErrorToStr (status));
		}
#if !defined(USE_POLARSSL)
		gnutls_global_deinit ();
	} else {
		flog (LOG_ERROR, "initialize_libraries: gnutls_global_init: %s", gcry_strerror (crystatus));

	}
#endif
	return false;
}

/* Create a Football service for pianod */
static bool init_server (APPSTATE *app) {
    /* Initialize TLS stuff for service */
	char path[PATH_MAX];

	settings_get_config_dir (PACKAGE, "", path, sizeof (path));
    if (!fb_init_tls_support (path)) {
        app->settings.https_port = 0;
    }

	/* Create a service */
    FB_SERVICE_OPTIONS options;
    memset (&options, 0, sizeof (options));
    options.line_port = app->settings.port;
    options.http_port = app->settings.http_port;
    options.https_port = app->settings.https_port;
    options.queue_size = 5;
    options.greeting_mode = FB_GREETING_ALLOW;
    options.context_size = sizeof (USER_CONTEXT);
    options.serve_directory = app->settings.client_location;
    options.name = "pianod";
	if ((app->service = fb_create_service (&options))) {
		return true;
	}
	flog (LOG_ERROR, "Unable to create service, giving up.\n");
	return false;
	
}




static void usage () {
	fprintf (stderr, "Usage: %s [-v] [-n user] [-g groups]  [-p port] [-i startscript] [-u userfile] [-c clientdir]\n"
			 "  -v            : Display version and exit.\n"
			 "  -n user       : the user pianod should change to when run as root\n"
			 "  -g groups     : supplementary groups pianod should use when run as root\n"
			 "  -p port       : the line-oriented port on which to run (default 4445); 0 to disable\n"
			 "  -P httpport   : the HTTP/greeted port on which to run (default 4446 or -p+1; 0 to disable)\n"
			 "  -s httpsport  : the HTTP Secure port (default 4447 or httpport+1; 0 to disable)\n"
			 "  -i initscript : the initialization script\n"
			 "                  (default ~/.config/pianod/startscript)\n"
			 "  -u userfile   : the location of the user/password file\n"
			 "                  (default ~/.config/pianod/passwd)\n"
			 "  -c clientdir  : a directory with web client files be served\n"
#if defined(ENABLE_CAPTURE)
			 "  -m capturedir : a directory for stream capture\n"
#endif
			 , getprogname());
}



int main (int argc, char **argv) {
	static APPSTATE app;
	char startscriptname[PATH_MAX];
	char *startscript = startscriptname;
	char *nobody = "nobody";
	char *nobody_groups = NULL;
	
	int serverOnly = 0;
	int flag;
	DIR *dpath;

    setprogname(*argv);
	memset (&app, 0, sizeof (app));
	
	settings_get_config_dir (PACKAGE, "startscript", startscriptname, sizeof (startscriptname));
	settings_initialize (&app.settings);

	while ((flag = getopt (argc, argv, "vn:g:p:P:s:c:i:SZ:z:u:m:")) > 0) {
        int argval;
		switch (flag) {
			case 'S':
				serverOnly = 1;
				break;
            case 'c':
                app.settings.client_location = optarg;
                break;
			case 'p':
                argval = atoi (optarg);
                if (argval != 0 ) {
                    if (app.settings.http_port == app.settings.port + 1) {
                        app.settings.http_port = argval + 1;
                    }
                    if (app.settings.https_port == app.settings.port + 2) {
                        app.settings.https_port = argval + 2;
                    }
                }
				app.settings.port = argval;
				break;
            case 'P':
                argval = atoi (optarg);
                if (argval != 0 && app.settings.https_port == app.settings.http_port + 1) {
                    app.settings.https_port = ((argval == 80) ? 443 : argval + 1);
                }
                app.settings.http_port = argval;
                break;
            case 's':
                app.settings.https_port = atoi (optarg);
                break;
			case 'i':
				startscript = optarg;
				break;
			case 'n':
				nobody = optarg;
				break;
			case 'g':
				nobody_groups = optarg;
				break;
			case 'u':
				free (app.settings.user_file);
				app.settings.user_file = strdup (optarg);
			case 'Z':
				set_logging (strtol (optarg, NULL, 0));
				break;
            case 'z':
                fb_set_logging(strtol (optarg, NULL, 0), NULL);
                break;
			case 'v':
				fprintf (stderr, PACKAGE " version " VERSION "\n");
				exit (0);
				break;
			case '?':
				usage ();
				exit (1);
#if defined(ENABLE_CAPTURE)
	    case 'm':
		/* Validate path exists */
		dpath = opendir (optarg);
		if (dpath) {
			closedir (dpath);
			/* Directory exists - save it*/
			app.settings.capture_path = strdup (optarg);
			/* Validates malloc failure by ignoring path */
			app.settings.capture_pathlen = strlen (app.settings.capture_path);
		} else {
			/* Error - Maybe directory does not exist. */
			flog (LOG_ERROR, "Capture path error(%d): %s\n", errno, strerror(errno));
		}
		break;
#endif
	    }
	}
	if (!app.settings.user_file) {
		flog (LOG_ERROR, "malloc error during startup.\n");
	}
	select_nobody_user (nobody, nobody_groups);
	precreate_file (app.settings.user_file);
	users_restore (app.settings.user_file);
	
	if (initialize_libraries (&app)) {		
		/* If the server initialized, start up, otherwise give up. */
		if (init_parser (&app)) {
			if (init_server(&app)) {
				FB_EVENT *config = fb_accept_file (app.service, startscript);
				if (config) {
					USER_CONTEXT *fakeuser = (USER_CONTEXT *)config->context;
					assert (fakeuser);
					fakeuser->user = get_startscript_user();
				} else {
					/* Error already logged by football */
				}
				drop_root_privs();
				if (serverOnly) {
					/* Run football server only for testing */
					while (app.service) {
						run_service (&app); /* See if sockets need attention */
						if (app.quit_requested) {
							if (!app.quit_initiated) {
								fb_close_service (app.service);
								app.quit_initiated = 1;
							}
						}	
					}
				} else {
					signal (SIGHUP, receive_signal);
					signal (SIGINT, receive_signal);
					signal (SIGTERM, receive_signal);
#if !defined(HAVE_SO_NOSIGPIPE)
					signal (SIGPIPE, SIG_IGN);
#endif
					pianod_run_loop (&app);
				}
			}
			fb_parser_destroy (app.parser);
		}
#if defined(ENABLE_SHOUT)
		if (app.shoutcast)
			sc_close_service(app.shoutcast);
#endif
		users_persist (app.settings.user_file);
		users_destroy ();
		destroy_station_info_cache ();
		ao_shutdown ();
		PianoDestroy (&app.ph);
		PianoDestroyPlaylist (app.song_history);
		PianoDestroyPlaylist (app.playlist);
		WaitressFree (&app.waith);
#if !defined(USE_POLARSSL)
		gnutls_global_deinit ();
#endif
		settings_destroy (&app.settings);
	}
	
	
	return 0;
}
