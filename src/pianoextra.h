/*
 *  pianoextra.h
 *  pianod
 *
 *  Created by Perette Barella on 2012-03-14.
 *  Copyright 2012-2014 Devious Fish. All rights reserved.
 *
 */

#include <config.h>
#include <piano.h>

#ifndef _PIANOEXTRA_H
#define _PIANOEXTRA_H

extern char * PianoGetAudioQualityName (PianoAudioQuality_t quality);
extern PianoStation_t *PianoFindStationByName (PianoStation_t *stations, const char *searchStation);
extern PianoStation_t *PianoFindQuickMixStation (PianoStation_t *stations);
extern PianoSong_t *PianoFindSongById (PianoSong_t *songs, const char *searchSong);

#endif
