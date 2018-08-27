## Process this file with automake to produce Makefile.in

noinst_LIBRARIES	= libpiano.a

if ENABLE_DEBUG
libpiano_a_CFLAGS	= -g
else
request_assert          = -DNDEBUG
endif

# This contortion is necessary to minimize library customizations.
# Add defines to the command line since the library reads its
# private config.h, not the main one from configure.
if HAVE_JSON_JSON_H
have_json_json_h	= -DHAVE_JSON_JSON_H=1
endif
if HAVE_JSON_C_JSON_H
have_json_c_json_h	= -DHAVE_JSON_C_JSON_H=1
endif
if HAVE_JSON_H
have_json_h		= -DHAVE_JSON_H=1
endif

libpiano_a_CPPFLAGS	= $(json_CFLAGS) -D_GNU_SOURCE -I../include -DGCRYPT_NO_DEPRECATED $(request_assert) $(have_json_json_h) $(have_json_c_json_h) $(have_json_h)
libpiano_a_SOURCES	= crypt.c piano.c request.c response.c list.c \
			  config.h piano.h crypt.h piano_private.h


