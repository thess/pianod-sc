/*
 *  service.c
 *  Server-style UI for libpiano.
 *
 *  Created by Perette Barella on 2012-03-10.
 *  Copyright 2012-2014 Devious Fish. All rights reserved.
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

#ifndef _SUPPORT_H
#define _SUPPORT_H

#include <stdint.h>

#include <piano.h>
#include <waitress.h>
#include <fb_public.h>

#include "settings.h"
#include "player.h"
#include "pianod.h"
#include "response.h"

extern int BarUiPianoCall (APPSTATE * const, PianoRequestType_t,
		void *, PianoReturn_t *, WaitressReturn_t *);
extern bool piano_transaction (APPSTATE *app, FB_EVENT *event, PianoRequestType_t type, void *data);
extern void prepend_history (APPSTATE *app, PianoSong_t *song);
extern void purge_unselected_songs (APPSTATE *app);
extern void set_pandora_user (APPSTATE *app, FB_EVENT *replyto);
extern bool validate_station_list (APPSTATE *app, FB_EVENT *event, char * const*stations);
extern PianoSong_t *get_song_by_id_or_current (APPSTATE *app, FB_EVENT *event, const char *songid);
extern PianoStation_t *get_station_by_name_or_current (APPSTATE *app, FB_EVENT *event, const char *stationname);
extern bool pwn_station (APPSTATE *app, FB_EVENT *event, const char *stationId);
extern bool update_station_list (APPSTATE *app);
extern bool skips_are_available (APPSTATE *app, FB_EVENT *event, char *station);
extern void cancel_playback (APPSTATE *app);
extern void generate_test_tone (APPSTATE *app, FB_EVENT *event);

extern void report_setting (FB_EVENT *event, RESPONSE_CODE id, const char *setting);
extern bool change_setting (APPSTATE *app, FB_EVENT *event, char *newvalue, char **setting);
extern void report_fingerprint (FB_EVENT *event, const uint_8 *fingerprint);
extern bool change_fingerprint (APPSTATE *app, FB_EVENT *event, char * const newvalue);

#endif
