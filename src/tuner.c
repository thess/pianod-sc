/*
 *  tuner.c
 *  pianod - User station preferences and autotuning.
 *
 *  Created by Perette Barella on 2013-03-19.
 *  Copyright 2013-2014 Devious Fish. All rights reserved.
 *
 */

#include <string.h>
#include <strings.h>
#include <assert.h>
#include <fb_public.h>

#include <piano.h>
#include <ezxml.h>

#include "pianod.h"
#include "logging.h"
#include "response.h"
#include "users.h"
#include "support.h"
#include "tuner.h"



typedef enum station_rating_t {
	RATING_NEUTRAL = 0,
	RATING_GOOD = 1,
	RATING_BAD = 2
} STATION_RATING;

typedef struct rating_record_t {
	char *station_id;
	STATION_RATING rating;
} RATING_RECORD;

typedef struct station_preferences_t {
	size_t capacity;
	size_t count;
	RATING_RECORD *ratings;
} STATION_PREF;


static STATION_RATING get_station_rating_by_name (const char *name) {
	if (strcasecmp (name, "neutral") == 0) {
		return RATING_NEUTRAL;
	} else if (strcasecmp (name, "good") == 0) {
		return RATING_GOOD;
	} else if (strcasecmp (name, "bad") == 0) {
		return RATING_BAD;
	}
	assert (0); // Shouldn't happen, but could in corrupt file conditions
	return RATING_NEUTRAL;
}

static const char *station_rating_name (STATION_RATING rating) {
	switch (rating) {
		case RATING_NEUTRAL:
			return "neutral";
		case RATING_GOOD:
			return "good";
		case RATING_BAD:
			return "bad";
	}
	assert (0);
	return "neutral";
}

void destroy_station_preferences (STATION_PREF *pref) {
	if (pref) {
		for (unsigned i = 0; i < pref->count; i++) {
			free (pref->ratings [i].station_id);
		}
		free (pref);
	}
}

static int rating_comparator (const void *a, const void *b) {
	assert (a);
	assert (b);
	return strcmp (((RATING_RECORD *)a)->station_id,
				   ((RATING_RECORD *)b)->station_id);
}

/* Find a station rating within the station preferences structure.
   Return NULL if not it does not exist yet. */
static RATING_RECORD *find_station_rating (struct user_t *user, const char *station_id) {
	STATION_PREF *pref = get_station_preferences (user);
	if (!pref) {
		return NULL;
	}
	RATING_RECORD search;
	search.station_id = (char *) station_id;
	return bsearch (&search, pref->ratings, pref->count, sizeof (RATING_RECORD), rating_comparator);
}

/* Retrieve the rating for a user & station */
static STATION_RATING get_station_rating (struct user_t *user, const char *station_id) {
	assert (user);
	assert (station_id);
	RATING_RECORD *rating = find_station_rating (user, station_id);
	return rating ? rating->rating : RATING_NEUTRAL;
}

/* Add or update a station rating for a user. */
static bool set_station_rating (struct user_t *user, const char *station_id, STATION_RATING new_rating) {
	assert (new_rating == RATING_NEUTRAL || new_rating == RATING_GOOD || new_rating == RATING_BAD);
	RATING_RECORD *rating = find_station_rating (user, station_id);
	if ((!rating && new_rating == RATING_NEUTRAL) || (rating && new_rating == rating->rating)) {
		/* No change */
		return true;
	}
	STATION_PREF *pref = get_station_preferences (user);
	if (rating) {
		/* Update existing entry */
		rating->rating = new_rating;
		set_station_preferences (user, pref); /* Sets dirty flag */
		return true;
	}

	/* Allocate the preference structure if needed */
	if (!pref) {
		pref = calloc (sizeof (STATION_PREF), 1);
		if (!pref) {
			perror ("set_station_rating:calloc");
			return false;
		}
        set_station_preferences (user, pref); /* Attach to user record */
	}
	/* Expand the data collection if needed */
    if (!fb_expandcalloc((void **) &pref->ratings, &pref->capacity, pref->count + 1, sizeof (RATING_RECORD))) {
        perror ("set_station_rating:realloc");
        return false;
	}
	/* Add the rating to the collection */
	char *id = strdup (station_id);
	if (!id) {
		perror ("set_station_rating:strdup");
	}
	signed int i = pref->count;
	while (--i >= 0) {
		if (strcmp (id, pref->ratings [i].station_id) > 0 ) {
			break;
		}
		pref->ratings [i+1] = pref->ratings [i];
	}
	pref->ratings [i+1].station_id = id;
	pref->ratings [i+1].rating = new_rating;
	pref->count++;
	set_station_preferences (user, pref); /* Sets dirty flag */
	return true;
}

/* Send just the rating line */
void send_station_rating (FB_EVENT *event, const char *station_id) {
	assert (event);
	assert (station_id);
	USER_CONTEXT *context = event->context;
	STATION_RATING rating = RATING_NEUTRAL;
	if (context->user) {
		rating = get_station_rating (context->user, station_id);
	}
	data_reply (event, I_USERRATING, station_rating_name (rating));
}

/* Send a list of ratings for a particular user */
void send_station_ratings (APPSTATE *app, FB_EVENT *event, struct user_t *user) {
	assert (app);
	assert (event);
	assert (user);

	PianoStation_t *station = app->ph.stations;
	if (event->argv [2]) {
		if ((station = get_station_by_name_or_current (app, event, event->argv[2]))) {
			reply (event, S_DATA);
			data_reply (event, I_STATION, station->name);
			send_station_rating (event, station->id);
			reply (event, S_DATA_END);
		}
	} else {
		PianoListForeachP (station) {
			reply (event, S_DATA);
			data_reply (event, I_STATION, station->name);
			send_station_rating (event, station->id);
		}
		reply (event, S_DATA_END);
	}
}


/* Send the current station rating to a specific user, or
   individually announce ratings to all users if user == NULL */
void announce_station_ratings (APPSTATE *app, struct user_t *user) {
	assert (app);
	assert (app->current_song);
	FB_ITERATOR *it = fb_new_iterator (app->service);
	if (it) {
		FB_EVENT *event;
		while ((event = fb_iterate_next (it))) {
			USER_CONTEXT *context = event->context;
			bool send = context->user ? (user == NULL) || (user == context->user)
									  : (user == NULL);
			if (send) {
				send_station_rating (event, app->current_song->stationId);
			}
		}
		fb_destroy_iterator (it);
	}
}


/* Process command to rate a station */
void rate_station (APPSTATE *app, FB_EVENT *event, struct user_t *user) {
	assert (app);
	assert (event);
	assert (user);
	STATION_RATING rating = get_station_rating_by_name (event->argv [2]);
	PianoStation_t *station = get_station_by_name_or_current (app, event, event->argv [3]);
	if (station) {
		reply (event, set_station_rating (user, station->id, rating) ? S_OK : E_NAK);
		if (app->selected_station && app->selected_station->isQuickMix && app->current_song &&
			strcmp (app->selected_station->id, app->current_song->stationId) == 0) {
			announce_station_ratings (app, user);
		}
		/* Notify all of this user's sessions that the ratings changed */
		FB_ITERATOR *it = fb_new_iterator (app->service);
		if (it) {
			FB_EVENT *notify;
			while ((notify = fb_iterate_next (it))) {
				USER_CONTEXT *context = notify->context;
				if (context->user == user) {
					reply (notify, I_USERRATINGS_CHANGED);
				}
			}
			fb_destroy_iterator (it);
		}
		recompute_stations (app);
	}
}

/* Persist station preferences as XML */
void persist_station_preferences (FILE *dest, struct user_t *user) {
	assert (dest);
	assert (user);
	STATION_PREF *prefs = get_station_preferences (user);

	if (prefs) {
		bool first = true;
		for (int i = 0; i < prefs->count; i++) {
			// Only write good/bad preferences; neutral is the default.
			if (prefs->ratings [i].rating != RATING_NEUTRAL) {
				if (first) {
					first = false;
					fprintf (dest, "    <stationpreferences>\n");
				}
				fprintf (dest, "      <station id='%s' rating='%s' />\n",
						 prefs->ratings [i].station_id,
						 station_rating_name (prefs->ratings [i].rating));
			}
		}
		if (!first) {
			fprintf (dest, "    </stationpreferences>\n");
		}
	}
}

/* Restore the station preferences from XML data */
bool recreate_station_preferences (struct user_t *user, ezxml_t data) {
	assert (user);
	assert (data);
	bool ok = true;
	
	for (ezxml_t pref = ezxml_child (data, "station"); pref; pref = pref->next) {
		const char *station_id = ezxml_attr (pref, "id");
		const char *rating_name = ezxml_attr (pref, "rating");
		if (station_id && rating_name) {
			set_station_rating (user, station_id, get_station_rating_by_name (rating_name));
		} else {
			flog (LOG_ERROR, "Station preference data corrupt for user %s\n", get_user_name (user));
			ok = false;
		}
	}
	return ok;
}

typedef struct station_selector_t {
	char *station_id;
	PianoStation_t *station;
	bool pure_good;
	bool partial_good;
	bool bad;
	bool include;
} STATION_SELECTOR;


static void apply_station_ratings (struct user_t *user, size_t station_count, STATION_SELECTOR *stations) {
	/* Require influence privilege to be considered in calculation */
	if (!have_privilege (user, PRIVILEGE_INFLUENCE)) {
		return;
	}
	/* Get and apply each station's rating for this user. */
	for (int i = 0; i < station_count; i++) {
		STATION_RATING rating = get_station_rating(user,
												   stations [i].station_id);
		switch (rating) {
			case RATING_GOOD:
				stations [i].partial_good = true;
				break;
			case RATING_BAD:
				stations [i].bad = true;
				/* FALLTHRU */
			case RATING_NEUTRAL:
				stations [i].pure_good = false;
				break;
			default:
				assert (0);
				break;
		}
	}
}


/* Apply station preferences for logged-in users */
static bool tune_based_on_logins (APPSTATE *app, size_t station_count, STATION_SELECTOR *stations) {
    assert (app);

	/* Iterate over connected users, and add their preferences into the station selection data */
	FB_ITERATOR *it = fb_new_iterator (app->service);
	bool anyone_listening = false;
	if (it) {
		FB_EVENT *event;
		while ((event = fb_iterate_next (it))) {
			USER_CONTEXT *context = event->context;
			anyone_listening = anyone_listening || event->type == FB_EVENT_ITERATOR;
			if (event->type == FB_EVENT_ITERATOR /* Ignore closing connections */ &&
				context->user) {
				apply_station_ratings (context->user, station_count, stations);
			}
		}
		fb_destroy_iterator (it);
	}
	return anyone_listening;
}


/* Apply station preferences for useres who are attributed as present. */
static bool tune_based_on_attribute (APPSTATE *app, size_t station_count, STATION_SELECTOR *stations) {
    assert (app);

	struct user_t *user;
	
	/* Iterate over all users, and add their preferences into the station selection data
	   if they both have influence and are present. */
	bool anyone_listening = false;
	for (user = get_first_user(); user; user = get_next_user (user)) {
		if (have_privilege (user, ATTRIBUTE_PRESENT)) {
			anyone_listening = true;
			apply_station_ratings (user, station_count, stations);
		}
	}
	return anyone_listening;
}

/*
When in automatic mode, change the mix stations based on users logged in.
This algorithm takes, in preference order:
1. Intersection of all good-rated stations of logged-in users (ones everyone likes).
2. Union of all good-rated stations less union of bad-rated stations (ones somebody likes and others can tolerate).
3. Union of all good and neutral stations less union of bad-rated stations (ones everyone can tolerate).
4. Music stops (cannot agree on the music).
*/
static bool station_computation_had_results;
static bool station_computation_has_listeners;
void recompute_stations (APPSTATE *app) {
	assert (app);
	if (!app->automatic_stations) {
		return;
	}

	size_t station_count = PianoListCountP (app->ph.stations);
	if (station_count == 0) {
		return;
	}
	
	STATION_SELECTOR *stations = calloc (sizeof (STATION_SELECTOR), station_count);
	if (!stations) {
		perror ("recompute_stations:calloc");
		return;
	}

	/* Copy the stations into an array that we can access efficiently,
	   with fields to store computations. */
	unsigned int i = 0;
	PianoStation_t *station = app->ph.stations;
	PianoListForeachP (station) {
		stations [i].station = station;
		stations [i].station_id = station->id;
		stations [i].pure_good = true;
		i++;
	}
	assert (i == station_count);

	/* Apply the selected autotuning algorithms */
	station_computation_has_listeners = false;
	if (app->settings.automatic_mode & TUNE_ON_LOGINS) {
		station_computation_has_listeners = tune_based_on_logins (app, station_count, stations) || station_computation_has_listeners;
	}
	if (app->settings.automatic_mode & TUNE_ON_ATTRIBUTE) {
		station_computation_has_listeners = tune_based_on_attribute (app, station_count, stations) || station_computation_has_listeners;
	}
	
	/* We now have a station list with a set of flags. */
	station_computation_had_results = !station_computation_has_listeners;
	int algorithm;
	for (algorithm = 1; algorithm <= 3; algorithm++) {
		for (i = 0; i < station_count; i++) {
			switch (algorithm) {
				case 1:
					stations[i].include = stations [i].pure_good;
					break;
				case 2:
					stations[i].include = stations [i].partial_good && !stations [i].bad;
					break;
				case 3:
					stations[i].include = !stations [i].bad;
					break;
				default:
					assert (0);
					break;
			}
			if (stations [i].include) {
				station_computation_had_results = true;
			}
		}
		if (station_computation_had_results) {
			break;
		}
	}
	
	if (!station_computation_has_listeners) {
		send_status (app->service, "No listeners.");
	} else if (station_computation_had_results && station_computation_has_listeners) {
		/* Copy the computed stations back into the libpiano structures. */
		bool changed = false;
		i = 0;
		station = app->ph.stations;
		PianoListForeachP (station) {
			if (stations [i].station->useQuickMix != stations [i].include) {
				changed = true;
				stations [i].station->useQuickMix = stations [i].include;
			}
			i++;
		}
		assert (i == station_count);
		if (changed) {
			piano_transaction (app, NULL, PIANO_REQUEST_SET_QUICKMIX, NULL);
			send_response (app->service, I_MIX_CHANGED);
			send_status (app->service,
						 algorithm == 1 ? "autotuner selected stations everyone likes" :
						 algorithm == 2 ? "autotuner selected stations somebody likes" :
						 "autotuner selected tolerable stations");
		}
	} else {
		send_status (app->service, "current listener station preferences are incompatible");
	}
	
	free (stations);
}


bool computed_stations_is_empty_set (void) {
	return (!station_computation_had_results || !station_computation_has_listeners);
}
