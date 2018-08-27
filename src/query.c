/*
 *  query.c
 *  Server-style UI for libpiano.
 *
 *  Created by Perette Barella on 2012-03-10.
 *  Copyright 2012-2014 Devious Fish. All rights reserved.
 *
 */

#ifndef __FreeBSD__
#define _POSIX_C_SOURCE 1 /* fileno() */
#define _DEFAULT_SOURCE /* strdup() */
#define _DARWIN_C_SOURCE /* strdup() on OS X */
#endif

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include <getopt.h>


#include <fb_public.h>
#include <piano.h>

#include "command.h"
#include "support.h"
#include "pianoextra.h"
#include "response.h"
#include "pianod.h"
#include "logging.h"
#include "pianod.h"
#include "seeds.h"
#include "users.h"
#include "query.h"


/* When the user issues a search, Pandora provides anything matching the term.
    If the user requires with the same term, use the existing results, even
    if the type (song, artist, anything) has changed. */
void destroy_search_context (USER_CONTEXT *context) {
	assert (context);
	if (context->search_results) {
		PianoDestroySearchResult (context->search_results);
		free (context->search_results);
		free (context->search_term);
		context->search_results = NULL;
		context->search_term = NULL;
	}
}


/* The genre list is retrieved once, without any qualifying search term.
   If a pianod user is searching, look through the genres and report matches. */
static void send_genres (APPSTATE *app, FB_EVENT *event, const char *search_genre) {
	/* Retrieve genre stations list if not yet available */
	if (app->ph.genreStations == NULL) {
		if (!piano_transaction (app, NULL, PIANO_REQUEST_GET_GENRE_STATIONS, NULL)) {
			return;
		}
	}


	PianoGenreCategory_t *category = app->ph.genreStations;
	PianoListForeachP (category) {
		bool categoryMatch = (strcasestr (category->name, search_genre) != NULL);
		PianoGenre_t *genre = category->genres;
		PianoListForeachP (genre) {
			bool genreMatch = (strcasestr (genre->name, search_genre) != NULL);
			if (categoryMatch || genreMatch) {
				reply (event, S_DATA);
				fb_fprintf (event, "%03d %s: %c%s\n", I_ID, Response (I_ID), (char) INFO_GENRESUGGESTION,
							genre->musicId);
				data_reply (event, I_GENRE, category->name);
				data_reply (event, I_STATION, genre->name);
			}
		}
	}
}


/* Process a FIND <genre|artist|song|any> command.  If search term is
   given, use that, otherwise repeat the last query. */
void perform_query (APPSTATE *app, FB_EVENT *event,	char *term)
{
	assert (app);
	assert (event);
	
	PianoRequestDataSearch_t reqData;
	USER_CONTEXT *context = (USER_CONTEXT *)event->context;

	/* Determine what to display. */
	bool showAny = strcasecmp (event->argv [1], "any") == 0;
	bool showSongs = strcasecmp (event->argv [1], "song") == 0;
	bool showArtists = strcasecmp (event->argv [1], "artist") == 0;
	bool showGenres = strcasecmp (event->argv [1], "genre") == 0;
	
	if (!term && !context->search_results) {
		data_reply (event, E_WRONG_STATE, "Search must be performed.");
		return;
	}

	/* If there's a search term, and it's different from previous term,
       retrieve a new list of suggestions. */
	if (term && (!context->search_term ||
                 strcasecmp (context->search_term, term) != 0)) {
		destroy_search_context (context);

		memset (&reqData, 0, sizeof (reqData));
		reqData.searchStr = term;
		if (!piano_transaction (app, NULL, PIANO_REQUEST_SEARCH, &reqData)) {
			reply (event, E_NAK);
			return;
		}
		context->search_term = strdup (event->argv [2]);
		if (!context->search_term) {
			data_reply (event, E_NAK, strerror (errno));
			return;
		}
		PianoSearchResult_t *newresults = malloc (sizeof (PianoSearchResult_t));
		if (!newresults) {
			data_reply (event, E_NAK, strerror (errno));
			return;
		}
		memcpy (newresults, &reqData.searchResult, sizeof (*newresults));
		context->search_results = newresults;
	}
	
	if (showAny || showArtists)
		send_artists (event, context->search_results->artists, INFO_ARTISTSUGGESTION);
	if (showAny || showSongs)
		send_songs_or_details (event, app, context->search_results->songs, INFO_SONGSUGGESTION);
	if (showAny || showGenres)
		send_genres (app, event, context->search_term);
	reply (event, S_DATA_END);
}

