## Process this file with automake to produce Makefile.in

if ENABLE_DEBUG
pianod_CFLAGS		= -g
else
request_assert          = -DNDEBUG
endif

# Memory mapping isn't needed for out little use and creates
# portability issues, so skip it.
libezxml_a_CPPFLAGS	= -D_GNU_SOURCE $(request_assert)
libezxml_a_CFLAGS	= -DEZXML_NOMMAP=1
noinst_LIBRARIES	= libezxml.a
libezxml_a_SOURCES	= ezxml.c ezxml.h



