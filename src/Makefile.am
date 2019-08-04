## Process this file with automake to produce Makefile.in

SUBDIRS		= include libpiano libwaitress libfootball libezxml 
bin_PROGRAMS	= pianod

if ENABLE_DEBUG
pianod_CFLAGS=-g
endif

pianod_CPPFLAGS	= -Iinclude -I.. -D_GNU_SOURCE -DGCRYPT_NO_DEPRECATED -DSYSCONFDIR='"$(sysconfdir)"'
pianod_LDFLAGS	= $(json_LIBS)
pianod_LDADD	= libwaitress/libwaitress.a libpiano/libpiano.a \
		  libfootball/libfootball.a libezxml/libezxml.a
pianod_SOURCES	= command.h logging.h pianod.h event.h \
		  pianoextra.h player.h query.h response.h \
		  seeds.h settings.h support.h tuner.h users.h lamercipher.c \
		  command.c logging.c pianod.c pianoextra.c event.c \
		  player.c query.c response.c seeds.c settings.c \
		  support.c tuner.c users.c id3tags.c

if ENABLE_SHOUT
pianod_SOURCES += threadqueue.h shoutcast.h threadqueue.c shoutcast.c
endif
