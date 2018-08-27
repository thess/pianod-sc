/*
 *  seeds.c
 *  Server-style UI for libpiano.
 *
 *  Created by Perette Barella on 2012-12-09
 *  Copyright 2012-2014 Devious Fish. All rights reserved.
 *
*/ 

#ifndef __FreeBSD__
#define _DEFAULT_SOURCE /* strdup() */
#define _DARWIN_C_SOURCE /* strdup() on OS X */
#endif

#include <config.h>

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <assert.h>

#include <piano.h>
#include <fb_public.h>

#include "support.h"
#include "seeds.h"
#include "response.h"
#include "pianoextra.h"


/* ------------ Station cache ------------- */
/* Track information doesn't contain details about seeds, seed IDs,
 or the feedback ID#s, etc.  This information is instead in a
 station information request.  The idea here is we'll request
 that info as needed, caching it for a few hours rather than
 repeatedly grabbing it.  Each time a playlist is grabbed, we'll
 patch it up with the additional information.  There will also
 be some convenience functions for getting bits that don't fit
 in the song records, like artist seeds.
 */

/* Cache for around 3 hours */
#define STATION_CACHE_TIME (10000)

typedef struct stationinfo_cache_t {
	char *station_id;
	time_t retrieved;
	PianoStationInfo_t *info;
	struct stationinfo_cache_t *next;
} STATIONINFO_CACHE;
 
STATIONINFO_CACHE *cache;



/* Retrieve station information such as seeds, feedback, etc. from Pandora. */
static PianoStationInfo_t *retrieve_station_info (APPSTATE *app, PianoStation_t *station) {
	assert (app);
	assert (station);
	
	PianoRequestDataGetStationInfo_t reqData;			
	memset (&reqData, 0, sizeof (reqData));
	reqData.station = station;
	
	if (piano_transaction (app, NULL, PIANO_REQUEST_GET_STATION_INFO, &reqData)) {
		PianoStationInfo_t *copy;
		if ((copy = malloc (sizeof (*copy)))) {
			memcpy(copy, &reqData.info, sizeof (*copy));
			return (copy);
		}
		perror ("retrieve_station_info:malloc");
		send_response_code (app->service, E_FAILURE, strerror(errno));
	}	
	PianoDestroyStationInfo (&reqData.info);
	return (NULL);
}


/* Create a new cache item with a station ID and the information. */
static STATIONINFO_CACHE *add_cache_record (APPSTATE *app, PianoStation_t *station, PianoStationInfo_t *item) {
	assert (app);
	assert (station);
	assert (item);
	
	STATIONINFO_CACHE *cacheitem;
	if ((cacheitem = malloc (sizeof (*cacheitem)))) {
		memset (cacheitem, 0, sizeof (*cacheitem));
		if ((cacheitem->station_id = strdup (station->id))) {
			cacheitem->retrieved = time(NULL);
			cacheitem->info = item;
			cacheitem->next = cache;
			cache = cacheitem;
			return (cacheitem);
		} else {
			perror ("create_cache_record:strdup");
			send_response_code (app->service, E_FAILURE, strerror (errno));
			free (cacheitem);
		}
	} else {			
		perror ("create_cache_record:malloc");
		send_response_code (app->service, E_FAILURE, strerror (errno));
	}
	return (NULL);
}


/* Release the cache */
void destroy_station_info_cache (void)
{
	STATIONINFO_CACHE *info;
	while ((info = cache)) {
		cache = info->next;
		PianoDestroyStationInfo (info->info);
		free (info->info);
		free (info->station_id);
		free (info);
	}
}




/* Retrieve a cached station, or NULL if it's not in the cache yet. */
static STATIONINFO_CACHE *get_cached_station (const char *station_id) {
	assert (station_id);
	
	for (STATIONINFO_CACHE *info = cache; info; info = info->next) {
		if (strcmp (info->station_id, station_id) == 0) {
			return (info);
		}
	}
	return (NULL);
}


/* Get station information from the cache, adding or refreshing the cache as necessary. */
static PianoStationInfo_t *get_station_info (APPSTATE *app, PianoStation_t *station) {
	assert (app);
	assert (station);
	
	STATIONINFO_CACHE *info = get_cached_station (station->id);

	/* If required or expired, get more info. */
	PianoStationInfo_t *newinfo = NULL;
	if (!info || (info->retrieved + STATION_CACHE_TIME) < time(NULL)) {
		newinfo = retrieve_station_info (app, station);
	}
	if (newinfo && info) {
		/* Update existing cached data */
		PianoDestroyStationInfo (info->info);
		free (info->info);
		info->info = newinfo;
		info->retrieved = time(NULL);
	} else if (newinfo) {
		/* Create new cache record */
		info = add_cache_record (app, station, newinfo);
		if (!info) {
			PianoDestroyStationInfo (newinfo);
			free (newinfo);
		}
	}
	return info ? info->info : NULL;
}


/* Get station info when all we have is a station ID, not a full station record */
static PianoStationInfo_t *get_station_info_by_id (APPSTATE *app, char *station_id) {
	assert (app);
	assert (station_id);
	
	PianoStation_t *station = PianoFindStationById (app->ph.stations, station_id);
	if (station) {
		return (get_station_info (app, station));
	}
	return (NULL);
}


/* Find a song in a playlist/song seed list/feedback list by
   artist name and song title */
static PianoSong_t *find_song_by_artist_and_title (PianoSong_t *songs,
												   const char *artist,
												   const char *title) {
	assert (artist);
	assert (title);
	
	PianoListForeachP (songs) {
		if (strcmp (songs->artist, artist) == 0 && strcmp (songs->title, title) == 0) {
			return songs;
		}
	}
	return NULL;
}


/* Find a artist in a list by name */
static PianoArtist_t *find_artist_by_name (PianoArtist_t *artist,
										   const char *artist_name) {
	/* For efficiency, find the comma once */
	const char *comma = strchr(artist_name, ',');
	assert (artist_name);
	PianoListForeachP (artist) {
		if (strcmp (artist->name, artist_name) == 0) {
			return artist;
		}
		/* To make our lives difficult, Pandora knows classical artist seeds
		   by first m. last, but songs are formatted with the artist last, first m. */
		if (comma) {
			const char *search = comma + 1;
			const char *compare = artist->name;
			/* Compare the first names */
			while (*search == ' ') {
				search++;
			}
			while (*search && *compare && *search == *compare) {
				search++, compare++;
			}
			/* We should be at end of last, first and at a space in first last */
			if (!*search && *compare == ' ') {
				search = artist_name;
				compare++;
				/* Compare surnames */
				while (*search && *compare && *search == *compare) {
					search++, compare++;
				}
				/* We should be at a comma in last, first and at the end of first last */
				if (*search == ',' && !*compare) {
					return artist;
				}
			}
		}
	}
	return NULL;
}



/* Update dest with new data, where dest is a pointer to char * */
static void update_field (char **dest, const char *newdata) {
	assert (dest);
	if (newdata) {
		if (*dest) {
			free (*dest);
		}
		*dest = strdup (newdata);
	} else {
		/* The item has been deleted from the station, so remove it from this song. */
		free (*dest);
		*dest = NULL;
	}
}

/* Search the cached station information for seed and feedback IDs, and apply
   them to a list of songs. */
static void apply_station_info_to_songs (APPSTATE *app, PianoSong_t *list) {
	PianoSong_t *song = list;
	PianoListForeachP (song) {
		PianoStationInfo_t *station_details = get_station_info_by_id (app, song->stationId);
		if (station_details) {
			PianoSong_t *info;
			/* Copy the feedback ID */
			info = find_song_by_artist_and_title(station_details->feedback,
												 song->artist, song->title);
			update_field (&song->feedbackId, info ? info->feedbackId : NULL);

			/* Copy the seed ID */
			info = find_song_by_artist_and_title(station_details->songSeeds,
												 song->artist, song->title);
			update_field (&song->seedId, info ? info->seedId : NULL);
		}
	}
}

/* Update the play history, current song, and playlist with
   new station deails */
void apply_station_info (APPSTATE *app) {
	apply_station_info_to_songs (app, app->song_history);
	apply_station_info_to_songs (app, app->current_song);
	apply_station_info_to_songs (app, app->playlist);
}



/* Expire and refresh station data in the cache */
static void expire_station_info_by_id (APPSTATE *app, char *station) {
	assert (station);
	
	STATIONINFO_CACHE *station_details = get_cached_station (station);
	if (station_details) {
		station_details->retrieved = 0;
		apply_station_info (app);
	}
}

static void expire_station_info (APPSTATE *app, PianoStation_t *station) {
	assert (station);
	expire_station_info_by_id (app, station->id);
}

static void expire_station_info_by_song (APPSTATE *app, const PianoSong_t *song) {
	assert (song);
	expire_station_info_by_id (app, song->stationId);
	if (song == app->current_song) {
		send_song_rating (app->service, song);
	}
}





/* Find artist seeds */
static PianoArtist_t* get_artist_seed_by_song (const PianoSong_t *song) {
	assert (song);
	assert (song->artist);
	assert (song->stationId);

	STATIONINFO_CACHE *station_details = get_cached_station (song->stationId);
	if (station_details) {
		return find_artist_by_name (station_details->info->artistSeeds, song->artist);
	}
	return NULL;
}

bool song_has_artist_seed (const PianoSong_t *song) {
	return (get_artist_seed_by_song (song) != NULL);
}






/* Display station information such as seeds, feedback, etc. */
void send_station_info (APPSTATE *app, FB_EVENT *event, PianoStation_t *station) {
	assert (app);
	assert (event);
	assert (station);
	
	PianoStationInfo_t *info = get_station_info (app, station);
	if (info) {
		PianoStation_t *seed_station = info->stationSeeds;
		PianoListForeachP (seed_station) {
			reply (event, S_DATA);
			fb_fprintf (event, "%03d %s: %c%s\n", I_ID, Response (I_ID),
						(char) INFO_STATIONSEED, seed_station->seedId);
			data_reply (event, I_STATION, seed_station->name);
			data_reply (event, I_RATING, "stationseed");
		}
		send_artists (event, info->artistSeeds, INFO_ARTISTSEED);
		send_songs_or_details (event, app, info->feedback, INFO_FEEDBACK);		
		send_songs_or_details (event, app, info->songSeeds, INFO_SONGSEED);
		reply(event, S_DATA_END);
	} else {
		reply (event, E_NAK);
	}	
}


/* Love, hate, or get sick of a song */
void rate_song (APPSTATE *app, FB_EVENT *event, const COMMAND cmd, const char *song_id) {
	PianoSong_t *song;
	bool expire = false;
	if ((song = get_song_by_id_or_current (app, event, song_id))) {
		if (cmd == RATEOVERPLAYED) {
			piano_transaction (app, event, PIANO_REQUEST_ADD_TIRED_SONG, song);
		} else if (cmd == RATENEUTRAL) {
			/* Make sure rating fields are up to date. */
			/* Otherwise, attempting to neutralize an inadvertent rate won't work. */
			apply_station_info (app);
			if (!song->feedbackId) {
				reply (event, song->rating == PIANO_RATE_NONE ? S_OK : E_NAK);
				return;
			}
			expire = piano_transaction (app, event, PIANO_REQUEST_DELETE_FEEDBACK, song);
			if (expire) {
				song->rating = PIANO_RATE_NONE;
			}
		} else {
			if (!pwn_station (app, event, song->stationId)) {
				return;
			}
			/* If it's already got this rating, skip the work. */
			PianoSongRating_t new_rating = cmd == RATELOVE ? PIANO_RATE_LOVE : PIANO_RATE_BAN;
			if (new_rating == song->rating) {
				reply (event, S_OK);
				return;
			}
			PianoRequestDataRateSong_t reqData;
			reqData.song = song;
			reqData.rating = new_rating;
			expire = piano_transaction (app, event, PIANO_REQUEST_RATE_SONG, &reqData);
		}
		if (expire) {
			expire_station_info_by_song (app, song);
		}
	}
}




/* Manage either artist or song seeds via the song. */
void remove_song_seed (APPSTATE *app, FB_EVENT *event, bool artist, const char *song_id) {
	assert (app);
	assert (event);
	PianoSong_t *song;
	if ((song = get_song_by_id_or_current (app, event, song_id))) {
		PianoRequestDataDeleteSeed_t reqData;
		memset (&reqData, 0, sizeof (reqData));
		if (artist) {
			reqData.artist = get_artist_seed_by_song (song);
			if (!reqData.artist) {
				data_reply (event, E_INVALID, "Song does not have an artist seed.");
				return;
			}
		} else {
			if (!song->seedId) {
				data_reply (event, E_INVALID, "Song does not have a song seed.");
				return;
			}
			reqData.song = song;
		}
		if (piano_transaction (app, event, PIANO_REQUEST_DELETE_SEED, &reqData)) {
			expire_station_info_by_song (app, song);
		}
	}
}



/* Add a seed via a suggestion ID */
void add_seed (APPSTATE *app, FB_EVENT *event, const char *stationname, char *suggestionid) {
	assert (app);
	assert (event);
	assert (suggestionid);

	STATION_INFO_TYPE type = *(suggestionid++);
	PianoRequestDataAddSeed_t reqData;
	if ((reqData.station = get_station_by_name_or_current(app, event, stationname))) {
		if (type == INFO_ARTISTSUGGESTION || type == INFO_SONGSUGGESTION ||
			type == INFO_GENRESUGGESTION) {
			if (!pwn_station(app, event, reqData.station->id)) {
				return;
			}
			reqData.musicId = suggestionid;
			if (piano_transaction (app, event, PIANO_REQUEST_ADD_SEED, &reqData)) {
				expire_station_info (app, reqData.station);
			}
		} else {
			reply (event, E_INVALID);
		}
	}
}


/* Add an artist or song seed directly from a song to a station */
void add_song_seed (APPSTATE *app, FB_EVENT *event,
								char *stationName, bool artist, const char *songid) {
	PianoSong_t *song;
	PianoRequestDataAddSeed_t seedReq;
	memset (&seedReq, 0, sizeof (seedReq));
	if ((song = get_song_by_id_or_current (app, event, songid))) {
		seedReq.station = stationName ? PianoFindStationByName(app->ph.stations, stationName)
									  : PianoFindStationById(app->ph.stations, song->stationId);
		if (!seedReq.station) {
			data_reply (event, E_NOTFOUND, "Station not found");
            return;
		}
		
		if (!pwn_station(app, event, seedReq.station->id)) {
			return;
		}
		PianoRequestDataSearch_t reqData;
		memset (&reqData, 0, sizeof (reqData));
		reqData.searchStr = artist ? song->artist : song->title;
		if (!piano_transaction (app, NULL, PIANO_REQUEST_SEARCH, &reqData)) {
			reply (event, E_NAK);
			return;
		}
		
		if (artist) {
			PianoArtist_t *seedartist = find_artist_by_name (reqData.searchResult.artists,
															 song->artist);
			if (seedartist) {
				seedReq.musicId = seedartist->musicId;
			}
		} else {
			PianoSong_t *seedsong = find_song_by_artist_and_title(reqData.searchResult.songs,
																  song->artist, song->title);
			if (seedsong) {
				seedReq.musicId = seedsong->musicId;
			}
		}
		if (seedReq.musicId) {
			if (piano_transaction (app, event, PIANO_REQUEST_ADD_SEED, &seedReq)) {
                expire_station_info (app, seedReq.station);
			}
		} else {
			reply (event, E_NAK);
		}
		PianoDestroySearchResult (&reqData.searchResult);
	}
}



/* Delete a seed via the ID provided with station info.
   Also works on feedback. */
void remove_seed (APPSTATE *app, FB_EVENT *event, const char *seedid) {
	assert (app);
	assert (event);
	assert (seedid);
	
	STATION_INFO_TYPE type = *(seedid++);
	PianoSong_t *feedback = NULL;
	PianoSong_t *songseed;
	PianoArtist_t *artistseed;
	PianoStation_t *stationseed;
	PianoRequestDataDeleteSeed_t reqData;
	memset (&reqData, 0, sizeof (reqData));
	bool success = false;

	STATIONINFO_CACHE *station = cache;
	while (station) {
		switch (type) {
			case INFO_FEEDBACK:
				feedback = station->info->feedback;
				PianoListForeachP (feedback) {
					if (strcmp (feedback->feedbackId, seedid) == 0) {
						break;
					}
				}
				break;
			case INFO_SONGSEED:
				songseed = station->info->songSeeds;
				PianoListForeachP (songseed) {
					if (strcmp (songseed->seedId, seedid) == 0) {
						reqData.song = songseed;
						break;
					}
				}
				break;
			case INFO_ARTISTSEED:
				artistseed = station->info->artistSeeds;
				PianoListForeachP (artistseed) {
					if (strcmp (artistseed->seedId, seedid) == 0) {
						reqData.artist = artistseed;
						break;
					}
				}
				break;
			case INFO_STATIONSEED:
				stationseed = station->info->stationSeeds;
				PianoListForeachP (stationseed) {
					if (strcmp (stationseed->seedId, seedid) == 0) {
						reqData.station = stationseed;
						break;
					}
				}
				break;
			default:
				reply (event, E_INVALID);
				return;
		}
		if (reqData.artist || reqData.song || reqData.station || feedback) {
			break;
		}
		station = station->next;
	}
	if (feedback) {
		/* We stopped while going through feedback, so this is feedback */
		success = piano_transaction (app, event, PIANO_REQUEST_DELETE_FEEDBACK, feedback);
	} else if (station) {
		/* We stopped before end of stations, and it's not feedback, so it's a seed */ 
		assert (reqData.artist || reqData.song || reqData.station);
		success = piano_transaction (app, event, PIANO_REQUEST_DELETE_SEED, &reqData);
	} else {
		reply (event, E_NOTFOUND);
	}
	if (success) {
		station->retrieved = 0;
		apply_station_info(app);
	}
}
