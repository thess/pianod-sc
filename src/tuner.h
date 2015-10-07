/*
 *  tuner.h
 *  pianod - User station preferences and autotuning.
 *
 *  Created by Perette Barella on 2013-03-19.
 *  Copyright 2013-2014 Devious Fish. All rights reserved.
 *
 */

#include <stdio.h>
#include <ezxml.h>

#include "users.h"

#ifndef _TUNER_H
#define _TUNER_H
struct station_preferences_t;

extern void rate_station (APPSTATE *app, FB_EVENT *event, struct user_t *user);
extern void send_station_rating (FB_EVENT *event, const char *station_id);
extern void send_station_ratings (APPSTATE *app, FB_EVENT *event, struct user_t *user);
extern void announce_station_ratings (APPSTATE *app, struct user_t *user);

extern void destroy_station_preferences (struct station_preferences_t *pref);
extern void persist_station_preferences (FILE *dest, struct user_t *user);
extern bool recreate_station_preferences (struct user_t *user, ezxml_t data);

extern bool computed_stations_is_empty_set (void);
extern void recompute_stations (APPSTATE *app);

#endif
