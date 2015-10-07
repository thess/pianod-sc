/*
 *  pianoextra.c
 *  pianod
 *
 *  Created by Perette Barella on 2012-03-14.
 *  Copyright 2012 Devious Fish. All rights reserved.
 *
 *	These are functions that probably belong in libpiano, but
 *	unless/until they get into that code base they're here.
 *
 */

#include <config.h>

#include <assert.h>
#include <string.h>
#include <strings.h>

#include "pianoextra.h"


/*
 * Get an audio quality setting name as text.
 * @param the audio quality level
 * @return the quality name as a string
 */
char * PianoGetAudioQualityName (PianoAudioQuality_t quality) {
	switch (quality) {
		case PIANO_AQ_LOW:
			return "low";
		case PIANO_AQ_MEDIUM:
			return "medium";
		case PIANO_AQ_HIGH:
			return "high";
        default:
            /* Continue on */
            break;
	}
	assert (0);
	return NULL;
}


/*	get station from list by name
 *	@param search here
 *	@param search for this
 *	@return the first station structure matching the given id
 */
PianoStation_t *PianoFindStationByName (PianoStation_t *stations, const char *searchStation) {
	assert (searchStation);
	PianoListForeachP (stations) {
		if (strcasecmp (stations->name, searchStation) == 0) {
			return stations;
		}
	}
	return NULL;
}

/*	get the quick mix station
 *	@param search here
 *	@return the quick mix station
 */
PianoStation_t *PianoFindQuickMixStation (PianoStation_t *stations) {
	PianoListForeachP (stations) {
		if (stations->isQuickMix) {
			return stations;
		}
	}
	return NULL;
}

/*	get song from list by ID (currently using track token)
 *	@param search here
 *	@param search for this
 *	@return the first song structure matching the given id
 */
PianoSong_t *PianoFindSongById (PianoSong_t *songs, const char *searchSong) {
	assert (searchSong);
	PianoListForeachP (songs) {
		if (strcmp (songs->trackToken, searchSong) == 0) {
			return songs;
		}
	}
	return NULL;
}
