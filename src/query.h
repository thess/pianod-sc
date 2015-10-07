/*
 *  query.h
 *  Server-style UI for libpiano.
 *
 *  Created by Perette Barella on 2012-03-10.
 *  Copyright 2012-2014 Devious Fish. All rights reserved.
 *
 */

#include "users.h"

#ifndef _QUERY_H
#define _QUERY_H

extern void destroy_search_context (USER_CONTEXT *context);
extern void perform_query (APPSTATE *app, FB_EVENT *event, char *term);

#endif
