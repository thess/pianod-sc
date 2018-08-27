## Process this file with automake to produce Makefile.in

if ENABLE_DEBUG
libfootball_a_CFLAGS	= -g
endif


noinst_LIBRARIES	= libfootball.a
libfootball_a_CPPFLAGS	= -Iinclude -I../.. -D_GNU_SOURCE
libfootball_a_SOURCES	= fb_event.c fb_parser.c fb_service.c fb_socketmgr.c \
			  fb_http.c fb_message.c fb_utility.c sha1.c \
			  fb_public.h fb_service.h sha1.h

