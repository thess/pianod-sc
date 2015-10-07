/*
 *  service.c
 *  Server-style UI for Piano.
 *
 *  Created by Perette Barella on 2012-03-10.
 *  Copyright 2012-2014 Devious Fish. All rights reserved.

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

#include <piano.h>
#include <waitress.h>

#include <fb_public.h>

#if defined(ENABLE_SHOUT)
#include "shoutcast.h"
#endif

#include "player.h"
#include "settings.h"


#ifndef _PIANOD_H
#define _PIANOD_H

typedef enum playback_state_t {
	PAUSED,
	PLAYING
} PLAYBACK_STATE;

typedef struct stallinfo_t {
	time_t since;
	time_t sample_time;
	int sample;
	bool stalled;
} STALLED;

typedef struct appstate_t {
	PianoHandle_t ph;
	WaitressHandle_t waith;
	struct audioPlayer player;
	BarSettings_t settings;
	PianoSong_t *playlist;
	time_t playlist_retrieved;
	PianoSong_t *current_song;
	PianoSong_t *song_history;
	PianoStation_t *selected_station;
	PLAYBACK_STATE playback_state;
	bool automatic_stations;
	time_t paused_since;
	STALLED stall;
	int player_soft_errors;
#if defined(ENABLE_SHOUT)
	sc_service *shoutcast;
#endif
	struct fb_service_t *service;
	struct fb_parser_t *parser;
	time_t retry_login_time;
	time_t update_station_list;
	bool pianoparam_change_pending;
	bool quit_requested;
	bool quit_initiated;
	bool broadcast_status;
} APPSTATE;



#endif
