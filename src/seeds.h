/*
 *  seeds.h
 *  Server-style UI for libpiano.
 *
 *  Created by Perette Barella on 2012-12-09.
 *  Copyright 2012-2014 Devious Fish. All rights reserved.
 *
*/
 
 #ifndef _SEEDS_H
 #define _SEEDS_H

#include "command.h"

/* To buffer clients from headaches of different seed types, we supply them
 with IDs with a prefix: 's' for song seed, 'S' for station seed,
 'a' for artist seed, 'f' for feedback. */
typedef enum station_info_types_t {
	INFO_SONG = 0,
	INFO_FEEDBACK = 'f',
	INFO_SONGSEED = 's',
	INFO_STATIONSEED = 't',
	INFO_ARTISTSEED = 'a',
	INFO_SONGSUGGESTION = 'S',
	INFO_ARTISTSUGGESTION = 'A',
	INFO_GENRESUGGESTION = 'G'
} STATION_INFO_TYPE;
 
extern void apply_station_info (APPSTATE *app);
extern void destroy_station_info_cache (void);

extern bool song_has_artist_seed (const PianoSong_t *song);

extern void send_station_info (APPSTATE *app, FB_EVENT *event, PianoStation_t *station);
extern void rate_song (APPSTATE *app, FB_EVENT *event, const COMMAND cmd, const char *song_id);
extern void remove_song_seed (APPSTATE *app, FB_EVENT *event, bool artist, const char *song_id);
extern void add_seed (APPSTATE *app, FB_EVENT *event, const char *stationname, char *suggestionid);
extern void add_song_seed (APPSTATE *app, FB_EVENT *event,
								char *stationName, bool artist, const char *songid);
extern void remove_seed (APPSTATE *app, FB_EVENT *event, const char *seedid);


#endif /* __SEEDS_H__ */
