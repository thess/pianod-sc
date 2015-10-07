/*
 *  logging.c
 *  pianod
 *
 *  Created by Perette Barella on 2012-03-16.
 *  Copyright 2012 Devious Fish. All rights reserved.
 *
 */

#include <config.h>

#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include "logging.h"

static LOG_TYPE logging = 0;
void set_logging (LOG_TYPE logtype) {
	logging = logtype;
}

/* If logging is enabled, log the time with a message. */
void vflog (LOG_TYPE level, const char *format, va_list parameters) {
	char date [22];
	if (level == 0 || (logging & level)) {
		time_t now = time (NULL);
		strftime(date, sizeof (date),"%Y-%m-%d %H:%M:%S", localtime(&now));
		fprintf (stderr, "%s: ", date);
		vfprintf(stderr, format, parameters);
        /* Protocol messages already have newlines on them */
        if (!(level & (LOG_100 | LOG_200 | LOG_300 | LOG_400 | LOG_500))) {
            fputc('\n', stderr);
        }
	}
}



/* If logging is enabled, log the time with a message. */
void flog (LOG_TYPE level, const char *format, ...) {
	va_list parameters;
	va_start(parameters, format);
	vflog (level, format, parameters);
	va_end(parameters);
}




