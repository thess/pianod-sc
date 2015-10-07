/*
 *  response.c - send messages of various kinds to clients
 *  pianod
 *
 *  Created by Perette Barella on 2012-03-16.
 *  Copyright 2012-2014 Devious Fish. All rights reserved.
 *
 */

#include <config.h>

#include <stdarg.h>
#include <assert.h>

#include <piano.h>
#include <fb_public.h>

#include "logging.h"
#include "response.h"
#include "pianod.h"
#include "command.h"
#include "users.h"
#include "tuner.h"


/* Log levels 0x02, 0x04, 0x08, 0x10, and 0x20 correspond
   to statuses 100,  200,  300,  400, and 500. */
static LOG_TYPE loglevel_of (RESPONSE_CODE response) {
	int level = response / 100;
	return 1 << ((level < 1 || level > 5) ? 5 : level);
}



static void sendflog (LOG_TYPE loglevel, void *there, const char *format, ...) {
	va_list parameters;
	va_start(parameters, format);
	vflog (loglevel, format, parameters);
	va_end(parameters);
	/* Need to reinitialize the var args data as it's modified by use */
	va_start(parameters, format);
	fb_vfprintf (there, format, parameters);
	va_end(parameters);
}



char *Response (RESPONSE_CODE response) {
	switch (response) {
		case I_WELCOME:			return "pianod " VERSION ". Welcome!";
		case I_PLAYING:			return "Playing";
		case I_STOPPED:			return "Stopped";
		case I_PAUSED:			return "Paused";
		case I_BETWEEN_TRACKS:	return "Intertrack";
		case I_STALLED:			return "Stalled";
		case I_TRACK_COMPLETE:	return "Track playback complete";
		case I_SELECTEDSTATION:	return "SelectedStation";
		case I_SELECTEDSTATION_NONE:
			return "No station selected";
		case I_ID:				return "ID";
		case I_ALBUM:			return "Album";
		case I_ARTIST:			return "Artist";
		case I_SONG:			return "Title";
		case I_STATION:			return "Station";
		case I_RATING:			return "Rating";
		case I_COVERART:		return "CoverArt";
		case I_GENRE:			return "Genre";
		case I_USERRATING:		return "UserRating";
		case I_CHOICEEXPLANATION:
								return "Explanation";
		case I_VOLUME:			return "Volume";
		case I_AUDIOQUALITY:	return "Quality";
#if defined(ENABLE_CAPTURE)
		case I_CAPTUREPATH:	return "CapturePath";
#endif
#if defined(ENABLE_SHOUT)
		case I_SHOUTCAST:	return "Shoutcast";
#endif
		case I_HISTORYSIZE:		return "HistoryLength";
		case I_AUTOTUNE_MODE:	return "AutotuneMode";
		case I_PAUSE_TIMEOUT:	return "PauseTimeout";
		case I_PLAYLIST_TIMEOUT:return "PlaylistTimeout";
		case I_PROXY:			return "Proxy";
		case I_CONTROLPROXY:	return "ControlProxy";
		case I_PARTNERUSER:		return "Partner";
		case I_PARTNERPASSWORD:	return "PartnerPassword";
		case I_RPCHOST:			return "RPCHost";
		case I_RPCTLSPORT:		return "RPCTLSPort";
		case I_TLSFINGERPRINT:	return "TlsFingerprint";
		case I_PANDORADEVICE:	return "DeviceType";
		case I_PANDORA_USER:	return "PandoraUser";
		case I_PANDORA_PASSWORD:return "PandoraPassword";
		case I_ENCRYPTION_PASSWORD:
								return "EncryptionPassword";
		case I_DECRYPTION_PASSWORD:
								return "DecryptionPassword";
		case I_OUTPUT_DRIVER:	return "OutputDriver";
		case I_OUTPUT_DEVICE:	return "OutputDevice";
		case I_OUTPUT_ID:		return "OutputID";
		case I_OUTPUT_SERVER:	return "OutputServer";
		case I_INFO_URL:		return "SeeAlso";
		case I_MIX_CHANGED:		return "Mix has been changed";
		case I_STATIONS_CHANGED:return "Station list has changed";
		case I_USER_PRIVILEGES:	return "Privileges";
		case I_USERRATINGS_CHANGED:
								return "User ratings have changed";
		case I_YELL:			return "says";
        case I_INFO:            return "Information";
        case I_SERVER_STATUS:   return "Status";
		case S_OK:				return "Success";
		case S_ANSWER_YES:		return "True, yes, 1, on";
		case S_ANSWER_NO:		return "False, no, 0, off";
		case S_DATA:			return "Data request ok";
		case S_DATA_END:		return "No data or end of data";
		case S_SIGNOFF:			return "Good-bye";
		case I_ERROR_DETAIL:	return "Detail";
		case I_PROXY_CONFIG:	return "Proxy settings invalid";
		case I_STATION_INVALID:	return "Invalid station";
		case I_NOTFOUND:		return "Item not found";
		case E_BAD_COMMAND:		return "Bad command";
		case E_UNAUTHORIZED:	return "Not authorized for requested action";
		case E_NAK:				return "Action failed";
		case E_DUPLICATE:		return "Already exists";
		case E_NOTFOUND:		return "Requested item not found";
		case E_WRONG_STATE:		return "Action is not applicable to current player state";
		case E_CREDENTIALS:		return "Invalid login or password";
		case E_REQUESTPENDING:	return "Temporary failure, future completion status unknown";
		case E_INVALID:			return "Invalid parameter";
		case E_TRANSFORM_FAILED:return "Station personalization failed";
		case E_QUOTA:			return "Quota exceeded";
		case E_LOGINREQUIRED:	return "Must be logged in";
		case E_CONFLICT:		return "Conflict encountered";
		case E_FAILURE:			return "Internal server error";
		case E_NOT_IMPLEMENTED:	return "Not implemented";
		case E_NETWORK_FAILURE:	return "Network failure ";
		case E_SHUTDOWN:		return "Service shutting down";
		case E_AUTHENTICATION:	return "Authentication failure";
		case E_RESOURCE:		return "Insufficent resources";
		case E_PANDORA:			return "Error communicating with Pandora";
		case E_INCOMPLETE:		return "Command execution incomplete";
			
		case A_SIGNED_IN:		return "signed in";
		case A_SIGNED_OUT:		return "has disconnected";
		case A_KICKED:			return "kicked";
		case A_SKIPPED:			return "skipped the song";
		case A_STOPPED:			return "stopped the player";
		case A_PAUSED:			return "paused playback";
		case A_RESUMED:			return "resumed playback";
		case A_CHANGED_MIX:		return "changed the mix";
		case A_MIX_ADDED:		return "added to the mix";
		case A_MIX_REMOVED:		return "removed from the mix";
		case A_SELECTED_STATION:return "selected the station";
		case A_CREATED_STATION:	return "created the station";
		case A_RENAMED_STATION:	return "renamed the station";
		case A_DELETED_STATION:	return "deleted the station";
        case A_PANDORA_SET:     return "set new Pandora credentials";
        case A_PANDORA_BORROW:  return "borrowed Pandora credentials from";

	}
	assert (0);
	flog (LOG_ERROR, "Unknown status %d", (int) response);
	return "Unknown status";
}



void reply (FB_EVENT *event, RESPONSE_CODE status) {
	sendflog (loglevel_of (status), event, "%03d %s\n", status, Response (status));
}

void data_reply (FB_EVENT *event, const RESPONSE_CODE status, const char *detail) {
	sendflog (loglevel_of (status), event, "%03d %s: %s\n", status, Response (status), detail);
}




/* Yes, this is almost identical to detailed_reply.
   But the name implies different context, and this is polymorphic. */
void send_data (void *there, const RESPONSE_CODE dataitem, const char *data) {
	if (data) {
		sendflog (loglevel_of (dataitem), there, "%03d %s: %s\n", dataitem, Response (dataitem), data);
	}
}

void send_response (void *there, RESPONSE_CODE code) {
	sendflog (loglevel_of (code), there, "%03d %s\n", code, Response (code));
}

void send_response_code (void *there, RESPONSE_CODE code, const char *message) {
	sendflog (loglevel_of (code), there, "%03d %s\n", code, message);
}

void send_status (void *there, const char *message) {
	sendflog (loglevel_of (I_SERVER_STATUS), there, "%03d %s\n", I_SERVER_STATUS, message);
}

void announce_action (FB_EVENT *event, APPSTATE *app, RESPONSE_CODE code, const char *parameter) {
	assert (event);
	assert (app);
	struct user_t *user = ((USER_CONTEXT *)event->context)->user;

	if (app->settings.broadcast_user_actions || code == I_YELL) {
		sendflog (LOG_USERACTION, event->service, "%03d %s %s%s%s\n", code == I_YELL ? I_YELL : I_INFO,
				  user ? get_user_name (user) : "A guest",
				  Response (code), parameter ? ": " : "", parameter ? parameter : "");
	} else {
		/* Log it in case action logging is turned on */
		flog (LOG_USERACTION, "%s %s%s%s\n", user ? get_user_name (user) : "A visitor",
			  Response (code), parameter ? ": " : "", parameter ? parameter : "");
		/* If status indicates something happened,  send the generic version of the message. */
	}
}

void send_selectedstation (void *there, APPSTATE *app) {
	if (app->selected_station) {
		sendflog (loglevel_of (I_SELECTEDSTATION), there, "%03d %s: %s %s\n",
				  I_SELECTEDSTATION, Response (I_SELECTEDSTATION),
				  app->selected_station->isQuickMix ? (app->automatic_stations ? "auto" : "mix") : "station",
				  app->selected_station->name);
	} else {
		send_response (there, I_SELECTEDSTATION_NONE);
	}
}






/* Send the current status, including:
   - Playing, paused, stopped, between tracks
   - Track duration, Current playhead position, track remaining if playing or paused
 */
void send_playback_status (void *there, APPSTATE *app) {
	/* Liberally adapted from PianoBar */
	if (app->player.mode >= PLAYER_SAMPLESIZE_INITIALIZED &&
		app->player.mode < PLAYER_FINISHED_PLAYBACK) {
		RESPONSE_CODE state = app->playback_state == PAUSED ? I_PAUSED :
							  app->stall.stalled ? I_STALLED : I_PLAYING;
		/* Ugly: songDuration is unsigned _long_ int! Lets hope this won't overflow */
		long songRemaining = (signed long int) (app->player.songDuration -
											   app->player.songPlayed) / BAR_PLAYER_MS_TO_S_FACTOR;
		enum {POSITIVE, NEGATIVE} sign = NEGATIVE;
		if (songRemaining < 0) {
			/* song is longer than expected */
			sign = POSITIVE;
			songRemaining = -songRemaining;
		}
		
		fb_fprintf (there, "%03d %02i:%02i/%02i:%02i/%c%02li:%02li %s\n",
					state,
					app->player.songPlayed / BAR_PLAYER_MS_TO_S_FACTOR / 60,
					app->player.songPlayed / BAR_PLAYER_MS_TO_S_FACTOR % 60,
					app->player.songDuration / BAR_PLAYER_MS_TO_S_FACTOR / 60,
					app->player.songDuration / BAR_PLAYER_MS_TO_S_FACTOR % 60,
					(sign == POSITIVE ? '+' : '-'),
					songRemaining / 60, songRemaining % 60,
					Response (state));	
	} else {
		send_response (there, app->playback_state == PLAYING && app->selected_station ? I_BETWEEN_TRACKS : I_STOPPED);
	}
}


static void send_seed_rating (void *there, const PianoSong_t *song, STATION_INFO_TYPE songtype) {
	const char *rating;
	switch (songtype) {
		case INFO_FEEDBACK:
			assert (song->rating != PIANO_RATE_NONE);
			rating = song->rating == PIANO_RATE_LOVE ? "good" : "bad";
			break;
		case INFO_SONGSEED:
			assert (song->seedId);
			rating = "seed";
			break;
		default:
			assert (0);
			return;
	}
	data_reply(there, I_RATING, rating);
}

void send_song_rating (void *there, const PianoSong_t *song) {
	char *rating = song->rating == PIANO_RATE_LOVE ? "good" :
				   song->rating == PIANO_RATE_BAN ? "bad" : "neutral";
	char *seed = song->seedId ? " seed" : "";
	char *artist_seed = song_has_artist_seed (song) ? " artistseed" : "";
	sendflog (loglevel_of (I_RATING), there, "%03d %s: %s%s%s\n", I_RATING, Response (I_RATING),
			  rating, seed, artist_seed);
}


/* Send a song or station detail record's worth of data */
static void send_song_or_detail_info (void *there, const APPSTATE *app, const PianoSong_t *song,
							   STATION_INFO_TYPE songtype) {
	assert (there);
	assert (app);
	assert (song);
	
	if (songtype == INFO_SONG) {
		send_data (there, I_ID, song->trackToken);
	} else {
		assert (songtype == INFO_FEEDBACK || songtype == INFO_SONGSEED || songtype == INFO_SONGSUGGESTION);
		/* Send the right whatever ID, prefixed with a type character so we can identify its type later */
		sendflog (loglevel_of (I_ID), there, "%03d %s: %c%s\n", I_ID, Response (I_ID),
				  (char) songtype, songtype == INFO_FEEDBACK ? song->feedbackId :
								   songtype == INFO_SONGSUGGESTION ? song->musicId : song->seedId);
	}
	send_data (there, I_ALBUM, song->album);
	send_data (there, I_ARTIST, song->artist);
	send_data (there, I_SONG, song->title);
	send_data (there, I_COVERART, song->coverArt);
	if (song->stationId) {
		PianoStation_t *station = PianoFindStationById (app->ph.stations, song->stationId);
		if (station) {
			send_data (there, I_STATION, station->name);
		}
	}
	if (songtype == INFO_SONG) {
		send_song_rating (there, song);
	} else if (songtype == INFO_FEEDBACK || songtype == INFO_SONGSEED) {
		send_seed_rating (there, song, songtype);
	} else {
		assert (songtype == INFO_SONGSUGGESTION);
	}
	send_data (there, I_INFO_URL, song->detailUrl);
}

/* Send a data for a standard song (as opposed to a station info pseudo-song) */
void send_song_info (void *there, const APPSTATE *app, const PianoSong_t *song) {
	send_song_or_detail_info (there, app, song, INFO_SONG);
}

/* Send a list of songs or station information records (no grouping messages) */
void send_songs_or_details (FB_EVENT *event, const APPSTATE *app, const PianoSong_t *song,
					 STATION_INFO_TYPE songtype) {
	PianoListForeachP (song) {
		reply (event, S_DATA);
		send_song_or_detail_info (event, app, song, songtype);
		if (songtype == INFO_SONG) {
			send_station_rating (event, song->stationId);
		}
	}
}

/* Send a list of standard songs (as opposed to station info pseudo-songs)
   bracketed by data grouping messages */
void send_song_list (FB_EVENT *event, const APPSTATE *app, const PianoSong_t *song) {
	send_songs_or_details (event, app, song, INFO_SONG);
	send_response_code (event, S_DATA_END, song ? "End of data" : "No data");
}


/* Send a list of stations bracketed by data messages */
void send_station_list (void *there, const PianoStation_t *station, const COMMAND cmd) {
	assert (there);

	if (station) {
		reply (there, S_DATA);
	}
	PianoListForeachP (station) {
		bool include;
		switch (cmd) {
			case STATIONLIST:
				include = true;
				break;
			case QUICKMIXINCLUDED:
				include = station->useQuickMix;
				break;
			case QUICKMIXEXCLUDED:
				include = !station->useQuickMix;
				break;
			default:
				assert (0);
				include = true;
				break;
		}
		if (include && !station->isQuickMix) {
			send_data (there, I_STATION, station->name);
		}
	}
	reply (there, S_DATA_END);
}




/* Send a list of artists */
void send_artists (void *there, const PianoArtist_t *artist, STATION_INFO_TYPE songtype) {
	assert (there);
	assert (songtype == INFO_ARTISTSEED || songtype == INFO_ARTISTSUGGESTION);
	PianoListForeachP (artist) {
		reply (there, S_DATA);
		fb_fprintf (there, "%03d %s: %c%s\n", I_ID, Response (I_ID), (char) songtype,
					songtype == INFO_ARTISTSEED ? artist->seedId : artist->musicId);
		data_reply (there, I_ARTIST, artist->name);
		if (songtype == INFO_ARTISTSEED) {
			data_reply (there, I_RATING, "artistseed");
		}
	}
}
