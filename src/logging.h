/*
 *  logging.h
 *  pianod
 *
 *  Created by Perette Barella on 2012-03-16.
 *  Copyright 2012-2014 Devious Fish. All rights reserved.
 *
 */

#include <config.h>
#include <stdarg.h>

#ifndef _LOGGING_H
#define _LOGGING_H

typedef enum log_types_t {
	LOG_ERROR = 0, /* Nonmaskable */
	LOG_GENERAL = 1,
	LOG_100 = 0x02, /* Corresponding to response message groups */
	LOG_200 = 0x04,
	LOG_300 = 0x08,
	LOG_400 = 0x10,
	LOG_500 = 0x20,
	/* 0x40 not used yet */
	LOG_STATUS = 0x80,
	LOG_EVENT = 0x100,
	LOG_WARNING = 0x200,
	LOG_COMMAND = 0x400,
	/* 0x800 not used yet */
	LOG_USERACTION = 0x1000
} LOG_TYPE;

/* Logging */
extern void set_logging (LOG_TYPE logtype);
extern void vflog (LOG_TYPE level, const char *format, va_list parameters);
extern void flog (LOG_TYPE level, const char *format, ...); /* A little S&M is always good... */

#endif
