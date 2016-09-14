#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([pianod],
	174-sc,
	[pianod@lists.deviousfish.com],
	,[http://deviousfish.com/pianod])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AX_CHECK_CFLAGS([-std=c99])
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_RANLIB
AM_PROG_AR

# Checks for libraries.
# Format: AC_CHECK_LIB([library], [function in that library])
# AC_CHECK_LIB([football], [fb_create_service])
# AC_CHECK_LIB([piano], [PianoRequest])
# Standard/common libraries:
AC_SEARCH_LIBS([pow], [m])
AC_CHECK_LIB([pthread], [pthread_create],,
	[AC_MSG_ERROR([POSIX thread support required],1)])

# pkg-config is sometimes used, but not always.  Try to find linker flags
# with it, but don't consider it an error.
# PKG_CHECK_MODULES only validates that pkg-config returns stuff; it doesn't
# validate that these are correct, so check lib to make sure they're right.
PKG_CHECK_MODULES([ao], [ao],,
	[AC_MSG_WARN([No pkg-config for libao])])
PKG_CHECK_MODULES([mad], [mad],,
	[AC_MSG_WARN([No pkg-config for libmad])])
PKG_CHECK_MODULES([json], [json-c],,
	[PKG_CHECK_MODULES([json], [json],,
		[PKG_CHECK_MODULES([json], [json0],,
			[AC_MSG_WARN([No pkg-config for json-c (aka libjson0, libjson)])])])])


# Network communication stuff:
AC_CHECK_LIB([gcrypt], [gcry_cipher_open],,
	[AC_MSG_ERROR([Cannot find required library: libgcrypt],1)])
# Check for SSL option
AC_ARG_WITH(mbedtls, [  --with-mbedtls	Build waitress with mbed TLS (default GNUTLS)], [
  AC_MSG_RESULT(>>Using mbedTLS)
  HAS_MBEDTLS=1
  AC_DEFINE([USE_MBEDTLS], 1, [Build for mbed TLS])
  PKG_CHECK_MODULES([mbedtls], [mbedtls],,
		[AC_MSG_WARN([No pkg-config for libmbedtls])])
  AC_CHECK_LIB([mbedtls], [mbedtls_ssl_set_session],,
		[AC_MSG_ERROR([Cannot find required library: libmbedtls],1)])
  AC_CHECK_LIB([mbedcrypto], [mbedtls_entropy_func],,
		[AC_MSG_ERROR([Cannot find required library: libmbedcrypto],1)])
  AC_CHECK_LIB([mbedx509], [mbedtls_x509_crt_parse_path],,
		[AC_MSG_ERROR([Cannot find required library: libmbedx509],1)]) ])
AM_CONDITIONAL([USE_MBEDTLS],[test "x$HAS_MBEDTLS" = "x1"])

if test "x$HAS_MBEDTLS" != x1 ; then
  PKG_CHECK_MODULES([gnutls], [gnutls],,
	[AC_MSG_WARN([No pkg-config for libgnutls])])
  AC_CHECK_LIB([gnutls], [gnutls_record_recv],,
	[AC_MSG_ERROR([Cannot find required library: libgnutls (aka gnutls)],1)])
  AC_CHECK_FUNCS(gnutls_transport_set_int2 gnutls_sec_param_to_pk_bits)
fi

# Bloody json library may be linked via -ljson, or -ljson-c depending on
# the platform.  Try both.
AC_CHECK_LIB([json], [json_object_new_string],,
	[AC_CHECK_LIB([json-c], [json_object_new_string],,
		[AC_MSG_ERROR([Cannot find required library: libjson-c (aka libjson0, libjson)])])
	])


# Audio-related libraries:
AC_CHECK_LIB([ao], [ao_play],,
	[AC_MSG_ERROR([Cannot find required library: libao],1)])
AC_CHECK_LIB([mad], [mad_stream_init])
if test "$ac_cv_lib_mad_mad_stream_init" = "yes"; then
	AC_DEFINE([ENABLE_MAD],[1],[Define if libmad is installed])
fi
AC_CHECK_LIB([faad], [NeAACDecOpen])
if test "$ac_cv_lib_faad_NeAACDecOpen" = "yes"; then
	AC_DEFINE([ENABLE_FAAD],[1],[Define if faad2 is installed])
fi
if test "$ac_cv_lib_mad_mad_stream_init" = "no" -a "$ac_cv_lib_faad_NeAACDecOpen" = "no"; then
	AC_MSG_ERROR([Cannot find required library: at least one of libfaad (faad2), libmad],1)
fi

# Stream capture
AC_ARG_WITH(capture, [  --with-capture          build with stream capture support (default without)], [
	AC_MSG_RESULT(>>Building with stream capture support)
	HAS_CAPTURE=1
	AC_DEFINE([ENABLE_CAPTURE], 1, [Build stream capture support])] )
AM_CONDITIONAL([ENABLE_CAPTURE],[test "x$HAS_CAPTURE" = "x1"])

# Shoutcast support
AC_ARG_WITH(shoutcast, [  --with-shoutcast        build with shoutcast support (default without)], [
	AC_MSG_RESULT(>>Building with shoutcast support)
	HAS_SHOUT=1
	AC_DEFINE([ENABLE_SHOUT], 1, [Build shoutcast support])
	AC_CHECK_LIB([shout], [shout_init],,
		[AC_MSG_ERROR([Cannot find required library: libshout],1)])] )
AM_CONDITIONAL([ENABLE_SHOUT],[test "x$HAS_SHOUT" = "x1"])

# Find password encryption library:
AC_SEARCH_LIBS([crypt],[crypt], [],
	[AC_MSG_ERROR([Cannot find required library: crypt (DES password encryption).])])

# Fgetln for Football:
# May be in standard libraries, in -lbsd on Linux, or not exist at all.
AC_SEARCH_LIBS([fgetln],[bsd],
	[AC_DEFINE([HAVE_FGETLN],[1],[Set to 1 if fgetln is available])])
AC_SEARCH_LIBS([getprogname],[bsd],
	[AC_DEFINE([HAVE_GETPROGNAME],[1],[Set to 1 if getprogname/setprogname is available])])

AM_CONDITIONAL([HAVE_FGETLN],[test "$ac_cv_search_fgetln" = 'none required'])

# IPv6 support
AC_MSG_CHECKING([PF_INET6/IPv6 support])
AC_TRY_COMPILE([#include <sys/socket.h>],
       [ int f = PF_INET6; ],
       [ AC_MSG_RESULT(yes)
	 AC_DEFINE(HAVE_IPV6, [1],
		   [Define this symbol if you have IPV6 support]) ],
       [ AC_MSG_RESULT(no)])

# res_init availability
AC_SEARCH_LIBS([res_init],[resolv])

AC_MSG_CHECKING([res_init() availability])
AC_LINK_IFELSE([AC_LANG_PROGRAM([
	#ifndef __FreeBSD__
	#define _BSD_SOURCE
	#endif
	#include <sys/types.h>
	#include <netinet/in.h>
	#include <arpa/nameser.h>
	#include <resolv.h> ],
	[ res_init() ])],
       [ AC_MSG_RESULT(yes)
	 AC_DEFINE(HAVE_RES_INIT, [1],
		   [Define this symbol if res_init() is available]) ],
       [ AC_MSG_RESULT(no)])


# Check for MSG_NOSIGNAL and SO_NOSIGPIPE socket options.
# Thanks to Philipp Kern <phil from 0x539.de>
# There are 'net references to an SO_NOSIGNAL, but I suspect it is an
# erroneous portmanteau of the other two.
AC_MSG_CHECKING([MSG_NOSIGNAL])
AC_TRY_COMPILE([#include <sys/socket.h>],
       [ int f = MSG_NOSIGNAL; ],
       [ AC_MSG_RESULT(yes)
	 AC_DEFINE(HAVE_MSG_NOSIGNAL, [1],
		   [Define this symbol if you have MSG_NOSIGNAL]) ],
       [ AC_MSG_RESULT(no)])

AC_MSG_CHECKING([SO_NOSIGPIPE])
AC_TRY_COMPILE([#include <sys/socket.h>],
       [ int f = SO_NOSIGPIPE; ],
       [ AC_MSG_RESULT(yes)
	 AC_DEFINE(HAVE_SO_NOSIGPIPE, [1],
		   [Define this symbol if you have SO_NOSIGPIPE]) ],
       [ AC_MSG_RESULT(no)])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h crypt.h gcrypt.h limits.h netdb.h netinet/in.h stdint.h stdlib.h string.h strings.h sys/socket.h unistd.h json-c/json.h json/json.h json.h])


AM_CONDITIONAL([HAVE_JSON_JSON_H],[test "$ac_cv_header_json_json_h" = 'yes'])
AM_CONDITIONAL([HAVE_JSON_C_JSON_H],[test "$ac_cv_header_json_c_json_h" = 'yes'])
AM_CONDITIONAL([HAVE_JSON_H],[test "$ac_cv_header_json_h" = 'yes'])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_C_RESTRICT
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memmove memset pow select socket strcasecmp strncasecmp strchr strdup strerror strrchr strtol strtoul])

# Build switches
AC_ARG_ENABLE(debug,
	[AS_HELP_STRING(
		[--enable-debug],
		[enable assertions and debugging]
	)]
)

AS_IF([test "$enable_debug" != "yes"], [
	AC_DEFINE([NDEBUG],[1],[Define to disable debugging assertions])
])
AM_CONDITIONAL([ENABLE_DEBUG],[test "$enable_debug" = 'yes'])

# There are presently no bug workarounds
# AC_ARG_ENABLE(bugs,
# 	[AS_HELP_STRING(
# 		[--enable-bugs],
# 		[enable bug workarounds]
# 	)]
# )
# AM_CONDITIONAL([ENABLE_BUGS],[test "$enable_bugs" = 'yes'])
# 
# AS_IF([test "$enable_bugs" = "yes"], [
# 	AC_DEFINE([ENABLE_BUGS],[1],[Define to enable bug workarounds])
# ])


# Write the files
AC_CONFIG_FILES([Makefile man/Makefile contrib/Makefile
		 src/Makefile src/include/Makefile
		 src/libezxml/Makefile src/libfootball/Makefile
		 src/libpiano/Makefile src/libwaitress/Makefile])
AC_OUTPUT
