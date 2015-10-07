## Process this file with automake to produce Makefile.in

noinst_LIBRARIES	= libwaitress.a

if ENABLE_DEBUG
libwaitress_a_CFLAGS	= -g
else
request_assert		= -DNDEBUG
endif

libwaitress_a_CPPFLAGS	= -D_GNU_SOURCE $(request_assert)
libwaitress_a_SOURCES	= waitress.c \
			  config.h waitress.h

