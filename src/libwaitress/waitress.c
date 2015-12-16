/*
Copyright (c) 2009-2013
	Lars-Dominik Braun <lars@6xq.net>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#ifndef __FreeBSD__
#define _POSIX_C_SOURCE 1 /* required by getaddrinfo() */
#define _BSD_SOURCE /* snprintf() */
#define _DARWIN_C_SOURCE /* snprintf() on OS X */
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>


#include "config.h"
#include "waitress.h"

#if defined(USE_POLARSSL)

#include <polarssl/ssl.h>
#include <polarssl/entropy.h>
#include <polarssl/ctr_drbg.h>
#include <polarssl/x509.h>
#include <polarssl/sha1.h>

struct _polarssl_ctx
{
	ssl_context		ssl;
	ssl_session		session;
	entropy_context		entrophy;
	ctr_drbg_context	rnd;
};

// Funky text declaration
#define _T(s) ((unsigned char *)s)

#else

// Use gnutls by default (USE_POLARSSL not defined)
#include <gnutls/x509.h>

#endif

#define strcaseeq(a,b) (strcasecmp(a,b) == 0)
#define WAITRESS_HTTP_VERSION "1.1"

typedef struct {
	char *data;
	size_t pos;
} WaitressFetchBufCbBuffer_t;

static WaitressReturn_t WaitressReceiveHeaders (WaitressHandle_t *, size_t *);

// gnutls wants (void *) and polarssl want (unsigned char *)
#if defined(USE_POLARSSL)
#define BUFFER_CAST unsigned char
#define RW_RETURN_TYPE int
#else
#define BUFFER_CAST void
#define RW_RETURN_TYPE ssize_t
#endif

#define READ_RET(buf, count, size) \
		if ((wRet = waith->request.read (waith, buf, count, size)) != \
				WAITRESS_RET_OK) { \
			return wRet; \
		}

#define WRITE_RET(buf, count) \
		if ((wRet = waith->request.write (waith, buf, count)) != WAITRESS_RET_OK) { \
			return wRet; \
		}

void WaitressInit (WaitressHandle_t *waith) {
	assert (waith != NULL);

	memset (waith, 0, sizeof (*waith));
	waith->timeout = 30000;
}

void WaitressFree (WaitressHandle_t *waith) {
	assert (waith != NULL);

	free (waith->url.url);
	free (waith->proxy.url);
	memset (waith, 0, sizeof (*waith));
}

/*	Proxy set up?
 *	@param Waitress handle
 *	@return true|false
 */
static bool WaitressProxyEnabled (const WaitressHandle_t *waith) {
	assert (waith != NULL);

	return waith->proxy.host != NULL;
}

/*	urlencode post-data
 *	@param encode this
 *	@return malloc'ed encoded string, don't forget to free it
 */
char *WaitressUrlEncode (const char *in) {
	assert (in != NULL);

	size_t inLen = strlen (in);
	/* worst case: encode all characters */
	char *out = calloc (inLen * 3 + 1, sizeof (*in));
	const char *inPos = in;
	char *outPos = out;

	while (inPos - in < inLen) {
		if (!isalnum (*inPos) && *inPos != '_' && *inPos != '-' && *inPos != '.') {
			*outPos++ = '%';
			snprintf (outPos, 3, "%02x", *inPos & 0xff);
			outPos += 2;
		} else {
			/* copy character */
			*outPos++ = *inPos;
		}
		++inPos;
	}

	return out;
}

/*	base64 encode data
 *	@param encode this
 *	@return malloc'ed string
 */
static char *WaitressBase64Encode (const char *in) {
	assert (in != NULL);

	size_t inLen = strlen (in);
	char *out, *outPos;
	const char *inPos;
	static const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz0123456789+/";
	const size_t alphabetLen = strlen (alphabet);

	/* worst case is 1.333 */
	out = malloc ((inLen * 2 + 1) * sizeof (*out));
	if (out == NULL) {
		return NULL;
	}
	outPos = out;
	inPos = in;

	while (inLen >= 3) {
		uint8_t idx;

		idx = ((*inPos) >> 2) & 0x3f;
		assert (idx < alphabetLen);
		*outPos = alphabet[idx];
		++outPos;

		idx = ((*inPos) & 0x3) << 4;
		++inPos;
		idx |= ((*inPos) >> 4) & 0xf;
		assert (idx < alphabetLen);
		*outPos = alphabet[idx];
		++outPos;

		idx = ((*inPos) & 0xf) << 2;
		++inPos;
		idx |= ((*inPos) >> 6) & 0x3;
		assert (idx < alphabetLen);
		*outPos = alphabet[idx];
		++outPos;

		idx = (*inPos) & 0x3f;
		++inPos;
		assert (idx < alphabetLen);
		*outPos = alphabet[idx];
		++outPos;

		inLen -= 3;
	}

	switch (inLen) {
		case 2: {
			uint8_t idx;

			idx = ((*inPos) >> 2) & 0x3f;
			assert (idx < alphabetLen);
			*outPos = alphabet[idx];
			++outPos;

			idx = ((*inPos) & 0x3) << 4;
			++inPos;
			idx |= ((*inPos) >> 4) & 0xf;
			assert (idx < alphabetLen);
			*outPos = alphabet[idx];
			++outPos;

			idx = ((*inPos) & 0xf) << 2;
			assert (idx < alphabetLen);
			*outPos = alphabet[idx];
			++outPos;

			*outPos = '=';
			++outPos;
			break;
		}

		case 1: {
			uint8_t idx;

			idx = ((*inPos) >> 2) & 0x3f;
			assert (idx < alphabetLen);
			*outPos = alphabet[idx];
			++outPos;

			idx = ((*inPos) & 0x3) << 4;
			assert (idx < alphabetLen);
			*outPos = alphabet[idx];
			++outPos;

			*outPos = '=';
			++outPos;

			*outPos = '=';
			++outPos;
			break;
		}
	}
	*outPos = '\0';

	return out;
}

/*	Split http url into host, port and path
 *	@param url
 *	@param returned url struct
 *	@return url is a http url? does not say anything about its validity!
 */
static bool WaitressSplitUrl (const char *inurl, WaitressUrl_t *retUrl) {
	assert (inurl != NULL);
	assert (retUrl != NULL);

	static const char *httpPrefix = "http://";

	/* is http url? */
	if (strncmp (httpPrefix, inurl, strlen (httpPrefix)) == 0) {
		enum {FIND_USER, FIND_PASS, FIND_HOST, FIND_PORT, FIND_PATH, DONE}
				state = FIND_USER, newState = FIND_USER;
		char *url, *urlPos, *assignStart;
		const char **assign = NULL;

		url = strdup (inurl);
		retUrl->url = url;

		urlPos = url + strlen (httpPrefix);
		assignStart = urlPos;

		if (*urlPos == '\0') {
			state = DONE;
		}

		while (state != DONE) {
			const char c = *urlPos;

			switch (state) {
				case FIND_USER: {
					if (c == ':') {
						assign = &retUrl->user;
						newState = FIND_PASS;
					} else if (c == '@') {
						assign = &retUrl->user;
						newState = FIND_HOST;
					} else if (c == '/') {
						/* not a user */
						assign = &retUrl->host;
						newState = FIND_PATH;
					} else if (c == '\0') {
						assign = &retUrl->host;
						newState = DONE;
					}
					break;
				}

				case FIND_PASS: {
					if (c == '@') {
						assign = &retUrl->password;
						newState = FIND_HOST;
					} else if (c == '/') {
						/* not a password */
						assign = &retUrl->port;
						newState = FIND_PATH;
					} else if (c == '\0') {
						assign = &retUrl->port;
						newState = DONE;
					}
					break;
				}

				case FIND_HOST: {
					if (c == ':') {
						assign = &retUrl->host;
						newState = FIND_PORT;
					} else if (c == '/') {
						assign = &retUrl->host;
						newState = FIND_PATH;
					} else if (c == '\0') {
						assign = &retUrl->host;
						newState = DONE;
					}
					break;
				}

				case FIND_PORT: {
					if (c == '/') {
						assign = &retUrl->port;
						newState = FIND_PATH;
					} else if (c == '\0') {
						assign = &retUrl->port;
						newState = DONE;
					}
					break;
				}

				case FIND_PATH: {
					if (c == '\0') {
						assign = &retUrl->path;
						newState = DONE;
					}
					break;
				}

				case DONE:
					break;
			} /* end switch */

			if (assign != NULL) {
				*assign = assignStart;
				*urlPos = '\0';
				assignStart = urlPos+1;

				state = newState;
				assign = NULL;
			}

			++urlPos;
		} /* end while */

		/* fixes for our state machine logic */
		if (retUrl->user != NULL && retUrl->host == NULL && retUrl->port != NULL) {
			retUrl->host = retUrl->user;
			retUrl->user = NULL;
		}
		return true;
	} /* end if strncmp */

	return false;
}

/*	Parse url and set host, port, path
 *	@param Waitress handle
 *	@param url: protocol://host:port/path
 */
bool WaitressSetUrl (WaitressHandle_t *waith, const char *url) {
	return WaitressSplitUrl (url, &waith->url);
}

/*	Set http proxy
 *	@param waitress handle
 *  @param url, e.g. http://proxy:80/
 */
bool WaitressSetProxy (WaitressHandle_t *waith, const char *url) {
	return WaitressSplitUrl (url, &waith->proxy);
}

/*	Callback for WaitressFetchBuf, appends received data to \0-terminated
 *	buffer
 *	@param received data
 *	@param data size
 *	@param buffer structure
 */
static WaitressCbReturn_t WaitressFetchBufCb (void *recvData, size_t recvDataSize,
		void *extraData) {
	char *recvBytes = recvData;
	WaitressFetchBufCbBuffer_t *buffer = extraData;

	if (buffer->data == NULL) {
		if ((buffer->data = malloc (sizeof (*buffer->data) *
				(recvDataSize + 1))) == NULL) {
			return WAITRESS_CB_RET_ERR;
		}
	} else {
		char *newbuf;
		if ((newbuf = realloc (buffer->data,
				sizeof (*buffer->data) *
				(buffer->pos + recvDataSize + 1))) == NULL) {
			free (buffer->data);
			return WAITRESS_CB_RET_ERR;
		}
		buffer->data = newbuf;
	}
	memcpy (buffer->data + buffer->pos, recvBytes, recvDataSize);
	buffer->pos += recvDataSize;
	buffer->data[buffer->pos] = '\0';

	return WAITRESS_CB_RET_OK;
}

/*	Fetch string. Beware! This overwrites your waith->data pointer
 *	@param waitress handle
 *	@param \0-terminated result buffer, malloced (don't forget to free it
 *			yourself)
 */
WaitressReturn_t WaitressFetchBuf (WaitressHandle_t *waith, char **retBuffer) {
	WaitressFetchBufCbBuffer_t buffer;
	WaitressReturn_t wRet;

	assert (waith != NULL);
	assert (retBuffer != NULL);

	memset (&buffer, 0, sizeof (buffer));

	waith->data = &buffer;
	waith->callback = WaitressFetchBufCb;

	wRet = WaitressFetchCall (waith);
	*retBuffer = buffer.data;
	return wRet;
}

/*	poll wrapper that retries after signal interrupts, required for socksify
 *	wrapper
 */
static int WaitressPollLoop (int fd, short events, int timeout) {
	int pollres = -1;
	struct pollfd sockpoll = {fd, events, 0};

	assert (fd != -1);

	do {
		errno = 0;
		pollres = poll (&sockpoll, 1, timeout);
	} while (errno == EINTR || errno == EINPROGRESS || errno == EAGAIN);

	return pollres;
}

/*	write () wrapper with poll () timeout
 *	@param waitress handle
 *	@param write buffer
 *	@param write count bytes
 *	@return number of written bytes or -1 on error
 */
static RW_RETURN_TYPE WaitressPollWrite (void *data, const BUFFER_CAST *buf, size_t count) {
	int pollres = -1;
	RW_RETURN_TYPE retSize;
	WaitressHandle_t *waith = data;

	assert (waith != NULL);
	assert (buf != NULL);

	/* FIXME: simplify logic */
	pollres = WaitressPollLoop (waith->request.sockfd, POLLOUT,
			waith->timeout);
	if (pollres == 0) {
		waith->request.readWriteRet = WAITRESS_RET_TIMEOUT;
		return -1;
	} else if (pollres == -1) {
		waith->request.readWriteRet = WAITRESS_RET_ERR;
		return -1;
	}
	if ((retSize = write (waith->request.sockfd, buf, count)) == -1) {
		waith->request.readWriteRet = WAITRESS_RET_ERR;
		return -1;
	}
	waith->request.readWriteRet = WAITRESS_RET_OK;
	return retSize;
}

static WaitressReturn_t WaitressOrdinaryWrite (void *data, const char *buf,
		const size_t size) {
	WaitressHandle_t *waith = data;

	WaitressPollWrite (waith, (BUFFER_CAST *)buf, size);
	return waith->request.readWriteRet;
}

static WaitressReturn_t WaitressTlsWrite (void *data, const char *buf,
		const size_t size) {
	WaitressHandle_t *waith = data;
#if defined(USE_POLARSSL)

	if (ssl_write (&waith->request.sslCtx->ssl, (BUFFER_CAST *)buf, size) < 0) {
		return WAITRESS_RET_TLS_WRITE_ERR;
	}
#else

	if (gnutls_record_send (waith->request.tlsSession, buf, size) < 0) {
		return WAITRESS_RET_TLS_WRITE_ERR;
	}
#endif
	return waith->request.readWriteRet;
}

/*	read () wrapper with poll () timeout
 *	@param waitress handle
 *	@param write to this buf, not NULL terminated
 *	@param buffer size
 *	@return number of read bytes or -1 on error
 */
static RW_RETURN_TYPE WaitressPollRead (void *data, BUFFER_CAST *buf, size_t count) {
	int pollres = -1;
	RW_RETURN_TYPE retSize;
	WaitressHandle_t *waith = data;

	assert (waith != NULL);
	assert (buf != NULL);

	/* FIXME: simplify logic */
	pollres = WaitressPollLoop (waith->request.sockfd, POLLIN, waith->timeout);
	if (pollres == 0) {
		waith->request.readWriteRet = WAITRESS_RET_TIMEOUT;
		return -1;
	} else if (pollres == -1) {
		waith->request.readWriteRet = WAITRESS_RET_ERR;
		return -1;
	}
	if ((retSize = read (waith->request.sockfd, buf, count)) == -1) {
		waith->request.readWriteRet = WAITRESS_RET_READ_ERR;
		return -1;
	}
	waith->request.readWriteRet = WAITRESS_RET_OK;
	return retSize;
}

static WaitressReturn_t WaitressOrdinaryRead (void *data, char *buf,
		const size_t size, size_t *retSize) {
	WaitressHandle_t *waith = data;

	const ssize_t ret = WaitressPollRead (waith, (BUFFER_CAST *)buf, size);
	if (ret != -1) {
		assert (ret >= 0);
		*retSize = (size_t) ret;
	}
	return waith->request.readWriteRet;
}

static WaitressReturn_t WaitressTlsRead (void *data, char *buf,
		const size_t size, size_t *retSize) {
	WaitressHandle_t *waith = data;

#if defined(USE_POLARSSL)
	int ret;

	*retSize = 0;
	waith->request.readWriteRet = WAITRESS_RET_OK;
	ret = ssl_read (&waith->request.sslCtx->ssl, (BUFFER_CAST *)buf, size);

	if (ret < 0) {
		if (ret != POLARSSL_ERR_SSL_PEER_CLOSE_NOTIFY) {
			waith->request.readWriteRet = WAITRESS_RET_TLS_READ_ERR;
		}

		return waith->request.readWriteRet;
	}

	*retSize = ret;
#else
	ssize_t ret = gnutls_record_recv (waith->request.tlsSession, buf, size);
	if (ret < 0) {
		return WAITRESS_RET_TLS_READ_ERR;
	} else {
		*retSize = ret;
	}
#endif
	return waith->request.readWriteRet;
}

/*	send basic http authorization
 *	@param waitress handle
 *	@param url containing user/password
 *	@param header name prefix
 */
static bool WaitressFormatAuthorization (WaitressHandle_t *waith,
		WaitressUrl_t *url, const char *prefix, char *writeBuf,
		const size_t writeBufSize) {
	assert (waith != NULL);
	assert (url != NULL);
	assert (prefix != NULL);
	assert (writeBuf != NULL);
	assert (writeBufSize > 0);

	if (url->user != NULL) {
		char userPass[1024], *encodedUserPass;
		snprintf (userPass, sizeof (userPass), "%s:%s", url->user,
				(url->password != NULL) ? url->password : "");
		encodedUserPass = WaitressBase64Encode (userPass);
		assert (encodedUserPass != NULL);
		snprintf (writeBuf, writeBufSize, "%sAuthorization: Basic %s\r\n",
				prefix, encodedUserPass);
		free (encodedUserPass);
		return true;
	}
	return false;
}

/*	get default http port if none was given
 */
static const char *WaitressDefaultPort (const WaitressUrl_t * const url) {
	assert (url != NULL);

	if (url->tls) {
		return url->tlsPort == NULL ? "443" : url->tlsPort;
	} else {
		return url->port == NULL ? "80" : url->port;
	}
}

/*	get line from string
 *	@param string beginning/return value of last call
 *	@return start of _next_ line or NULL if there is no next line
 */
static char *WaitressGetline (char * const str) {
	char *eol;

	assert (str != NULL);

	eol = strchr (str, '\n');
	if (eol == NULL) {
		return NULL;
	}

	/* make lines parseable by string routines */
	*eol = '\0';
	if (eol-1 >= str && *(eol-1) == '\r') {
		*(eol-1) = '\0';
	}
	/* skip \0 */
	++eol;

	assert (eol >= str);

	return eol;
}

/*	identity encoding handler
 */
static WaitressHandlerReturn_t WaitressHandleIdentity (void *data, char *buf,
		const size_t size) {
	assert (data != NULL);
	assert (buf != NULL);

	WaitressHandle_t *waith = data;

	waith->request.contentReceived += size;
	if (waith->callback (buf, size, waith->data) == WAITRESS_CB_RET_ERR) {
		return WAITRESS_HANDLER_ABORTED;
	} else {
		return WAITRESS_HANDLER_CONTINUE;
	}
}

/*	chunked encoding handler
 */
static WaitressHandlerReturn_t WaitressHandleChunked (void *data, char *buf,
		const size_t size) {
	assert (data != NULL);
	assert (buf != NULL);

	WaitressHandle_t * const waith = data;
	size_t pos = 0;

	while (pos < size) {
		switch (waith->request.chunkedState) {
			case CHUNKSIZE:
				/* Poor man’s hex to integer. This avoids another buffer that
				 * fills until the terminating \r\n is received. */
				if (buf[pos] >= '0' && buf[pos] <= '9') {
					waith->request.chunkSize <<= 4;
					waith->request.chunkSize |= buf[pos] & 0xf;
				} else if (buf[pos] >= 'a' && buf[pos] <= 'f') {
					waith->request.chunkSize <<= 4;
					waith->request.chunkSize |= (buf[pos]+9) & 0xf;
				} else if (buf[pos] == '\r') {
					/* ignore */
				} else if (buf[pos] == '\n') {
					waith->request.chunkedState = DATA;
					/* last chunk has size 0 */
					if (waith->request.chunkSize == 0) {
						return WAITRESS_HANDLER_DONE;
					}
				} else {
					/* everything else is a protocol violation */
					return WAITRESS_HANDLER_ERR;
				}
				++pos;
				break;

			case DATA:
				if (waith->request.chunkSize > 0) {
					assert (size >= pos);
					size_t payloadSize = size - pos;

					if (payloadSize > waith->request.chunkSize) {
						payloadSize = waith->request.chunkSize;
					}
					if (WaitressHandleIdentity (waith, &buf[pos],
							payloadSize) == WAITRESS_HANDLER_ABORTED) {
						return WAITRESS_HANDLER_ABORTED;
					}
					pos += payloadSize;
					assert (waith->request.chunkSize >= payloadSize);
					waith->request.chunkSize -= payloadSize;
				} else {
					/* next chunk size starts in the next line */
					if (buf[pos] == '\n') {
						waith->request.chunkedState = CHUNKSIZE;
					}
					++pos;
				}
				break;
		}
	}

	return WAITRESS_HANDLER_CONTINUE;
}

/*	handle http header
 */
static void WaitressHandleHeader (WaitressHandle_t *waith, const char * const key,
		const char * const value) {
	assert (waith != NULL);
	assert (key != NULL);
	assert (value != NULL);

	if (strcaseeq (key, "Content-Length")) {
		waith->request.contentLength = atol (value);
		waith->request.contentLengthKnown = true;
	} else if (strcaseeq (key, "Transfer-Encoding")) {
		if (strcaseeq (value, "chunked")) {
			waith->request.dataHandler = WaitressHandleChunked;
		}
	}
}

/*	parse http status line and return status code
 */
static int WaitressParseStatusline (const char * const line) {
	char status[4] = "000";

	assert (line != NULL);

	if (sscanf (line, "HTTP/1.%*1[0-9] %3[0-9] ", status) == 1) {
		return atoi (status);
	}
	return -1;
}

/*	verify server certificate
 */
static WaitressReturn_t WaitressTlsVerify (const WaitressHandle_t *waith) {

#if defined(USE_POLARSSL)
	unsigned char fingerprint[20];

	const x509_crt* cert = ssl_get_peer_cert (&waith->request.sslCtx->ssl);

	if (NULL == cert) {
		return WAITRESS_RET_TLS_HANDSHAKE_ERR;
	}

	sha1 (cert->raw.p, cert->raw.len, fingerprint);

	if (memcmp (fingerprint, waith->tlsFingerprint, sizeof (fingerprint)) != 0) {
		return WAITRESS_RET_TLS_FINGERPRINT_MISMATCH;
	}

#else
	gnutls_session_t session = waith->request.tlsSession;
	unsigned int certListSize;
	const gnutls_datum_t *certList;
	gnutls_x509_crt_t cert;
	char fingerprint[20];

	if (gnutls_certificate_type_get (session) != GNUTLS_CRT_X509) {
		return WAITRESS_RET_TLS_HANDSHAKE_ERR;
	}

	if ((certList = gnutls_certificate_get_peers (session,
			&certListSize)) == NULL) {
		return WAITRESS_RET_TLS_HANDSHAKE_ERR;
	}

	if (gnutls_x509_crt_init (&cert) != GNUTLS_E_SUCCESS) {
		return WAITRESS_RET_TLS_HANDSHAKE_ERR;
	}

	if (gnutls_x509_crt_import (cert, &certList[0],
			GNUTLS_X509_FMT_DER) != GNUTLS_E_SUCCESS) {
		return WAITRESS_RET_TLS_HANDSHAKE_ERR;
	}

	size_t fingerprintSize = sizeof (fingerprint);
	if (gnutls_x509_crt_get_fingerprint (cert, GNUTLS_DIG_SHA1, fingerprint,
			&fingerprintSize) != 0) {
		return WAITRESS_RET_TLS_HANDSHAKE_ERR;
	}

	assert (waith->tlsFingerprint != NULL);
	if (memcmp (fingerprint, waith->tlsFingerprint, sizeof (fingerprint)) != 0) {
		return WAITRESS_RET_TLS_FINGERPRINT_MISMATCH;
	}

	gnutls_x509_crt_deinit (cert);
#endif
	return WAITRESS_RET_OK;
}

/*	Connect to server
 */
static WaitressReturn_t WaitressConnect (WaitressHandle_t *waith) {
	WaitressReturn_t ret;
	struct addrinfo hints, *gares;

	memset (&hints, 0, sizeof hints);

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	/* Use proxy? */
	if (WaitressProxyEnabled (waith)) {
		if (getaddrinfo (waith->proxy.host,
				WaitressDefaultPort (&waith->proxy), &hints, &gares) != 0) {
			return WAITRESS_RET_GETADDR_ERR;
		}
	} else {
		if (getaddrinfo (waith->url.host,
				WaitressDefaultPort (&waith->url), &hints, &gares) != 0) {
			return WAITRESS_RET_GETADDR_ERR;
		}
	}

	/* try all addresses */
	for (struct addrinfo *gacurr = gares; gacurr != NULL;
			gacurr = gacurr->ai_next) {
		int sock = -1;

		ret = WAITRESS_RET_OK;

		if ((sock = socket (gacurr->ai_family, gacurr->ai_socktype,
				gacurr->ai_protocol)) == -1) {
			ret = WAITRESS_RET_SOCK_ERR;
		} else {
			int pollres;

			/* we need shorter timeouts for connect() */
			fcntl (sock, F_SETFL, O_NONBLOCK);

			/* increase socket receive buffer */
			const int sockopt = 256*1024;
			setsockopt (sock, SOL_SOCKET, SO_RCVBUF, &sockopt,
					sizeof (sockopt));

			/* non-blocking connect will return immediately */
			if (connect (sock, gacurr->ai_addr, gacurr->ai_addrlen) == -1) {
				// Error if not in-progress or immediate success
				if (errno != EINPROGRESS) {
					// Close socket and try alternatives
					close (sock);
					continue;
				}
			}

			pollres = WaitressPollLoop (sock, POLLOUT, waith->timeout);
			if (pollres == 0) {
				ret = WAITRESS_RET_TIMEOUT;
			} else if (pollres == -1) {
				ret = WAITRESS_RET_ERR;
			} else {
				/* check connect () return value */
				socklen_t pollresSize = sizeof (pollres);
				getsockopt (sock, SOL_SOCKET, SO_ERROR, &pollres,
						&pollresSize);
				if (pollres != 0) {
					ret = WAITRESS_RET_CONNECT_REFUSED;
				} else {
					/* this one is working */
					waith->request.sockfd = sock;
					break;
				}
			}
			close (sock);
		}
	}

	freeaddrinfo (gares);
	/* could not connect to any of the addresses */
	if (ret != WAITRESS_RET_OK) {
		return ret;
	}

	if (waith->url.tls) {
		WaitressReturn_t wRet;

		/* set up proxy tunnel */
		if (WaitressProxyEnabled (waith)) {
			char buf[256];
			size_t size;

			snprintf (buf, sizeof (buf), "CONNECT %s:%s HTTP/"
					WAITRESS_HTTP_VERSION "\r\n"
					"Host: %s:%s\r\n"
					"Proxy-Connection: close\r\n",
					waith->url.host, WaitressDefaultPort (&waith->url),
					waith->url.host, WaitressDefaultPort (&waith->url));
			WRITE_RET (buf, strlen (buf));

			/* write authorization headers */
			if (WaitressFormatAuthorization (waith, &waith->proxy, "Proxy-",
					buf, WAITRESS_BUFFER_SIZE)) {
				WRITE_RET (buf, strlen (buf));
			}

			WRITE_RET ("\r\n", 2);

			if ((wRet = WaitressReceiveHeaders (waith, &size)) !=
					WAITRESS_RET_OK) {
				return wRet;
			}
		}

#if defined(USE_POLARSSL)
		ssl_set_hostname (&waith->request.sslCtx->ssl, waith->url.host);
		if (ssl_handshake (&waith->request.sslCtx->ssl) != 0) {
			return WAITRESS_RET_TLS_HANDSHAKE_ERR;
		}
#else
		/* Ignore return code as connection will likely still succeed */
		gnutls_server_name_set (waith->request.tlsSession, GNUTLS_NAME_DNS,
				waith->url.host, strlen (waith->url.host));

		if (gnutls_handshake (waith->request.tlsSession) != GNUTLS_E_SUCCESS) {
			return WAITRESS_RET_TLS_HANDSHAKE_ERR;
		}
#endif

		if ((wRet = WaitressTlsVerify (waith)) != WAITRESS_RET_OK) {
			return wRet;
		}

		/* now we can talk encrypted */
		waith->request.read = WaitressTlsRead;
		waith->request.write = WaitressTlsWrite;
	}

	return WAITRESS_RET_OK;
}

/*	Write http header/post data to socket
 */
static WaitressReturn_t WaitressSendRequest (WaitressHandle_t *waith) {
	assert (waith != NULL);
	assert (waith->request.buf != NULL);

	const char *path = waith->url.path;
	char * const buf = waith->request.buf;
	WaitressReturn_t wRet = WAITRESS_RET_OK;

	if (waith->url.path == NULL) {
		/* avoid NULL pointer deref */
		path = "";
	} else if (waith->url.path[0] == '/') {
		/* most servers don't like "//" */
		++path;
	}

	/* send request */
	if (WaitressProxyEnabled (waith) && !waith->url.tls) {
		snprintf (buf, WAITRESS_BUFFER_SIZE,
			"%s http://%s:%s/%s HTTP/" WAITRESS_HTTP_VERSION "\r\n"
			"Host: %s\r\nUser-Agent: " LIBWAITRESS_NAME "\r\nConnection: Close\r\n",
			(waith->method == WAITRESS_METHOD_GET ? "GET" : "POST"),
			waith->url.host,
			WaitressDefaultPort (&waith->url), path, waith->url.host);
	} else {
		snprintf (buf, WAITRESS_BUFFER_SIZE,
			"%s /%s HTTP/" WAITRESS_HTTP_VERSION "\r\n"
			"Host: %s\r\nUser-Agent: " LIBWAITRESS_NAME "\r\nConnection: Close\r\n",
			(waith->method == WAITRESS_METHOD_GET ? "GET" : "POST"),
			path, waith->url.host);
	}
	WRITE_RET (buf, strlen (buf));

	if (waith->method == WAITRESS_METHOD_POST && waith->postData != NULL) {
		snprintf (buf, WAITRESS_BUFFER_SIZE, "Content-Length: %zu\r\n",
				strlen (waith->postData));
		WRITE_RET (buf, strlen (buf));
	}

	/* write authorization headers */
	if (WaitressFormatAuthorization (waith, &waith->url, "", buf,
			WAITRESS_BUFFER_SIZE)) {
		WRITE_RET (buf, strlen (buf));
	}
	/* don't leak proxy credentials to destination server if tls is used */
	if (!waith->url.tls &&
			WaitressFormatAuthorization (waith, &waith->proxy, "Proxy-",
			buf, WAITRESS_BUFFER_SIZE)) {
		WRITE_RET (buf, strlen (buf));
	}

	if (waith->extraHeaders != NULL) {
		WRITE_RET (waith->extraHeaders, strlen (waith->extraHeaders));
	}

	WRITE_RET ("\r\n", 2);

	if (waith->method == WAITRESS_METHOD_POST && waith->postData != NULL) {
		WRITE_RET (waith->postData, strlen (waith->postData));
	}

	return WAITRESS_RET_OK;
}

/*	receive response headers
 *	@param Waitress handle
 *	@param return unhandled bytes count in buf
 */
static WaitressReturn_t WaitressReceiveHeaders (WaitressHandle_t *waith,
		size_t *retRemaining) {
	char * const buf = waith->request.buf;
	size_t bufFilled = 0, recvSize = 0;
	char *nextLine = NULL, *thisLine = NULL;
	enum {HDRM_HEAD, HDRM_LINES, HDRM_FINISHED} hdrParseMode = HDRM_HEAD;
	WaitressReturn_t wRet = WAITRESS_RET_OK;

	/* receive answer */
	nextLine = buf;
	while (hdrParseMode != HDRM_FINISHED) {
		READ_RET (buf+bufFilled, WAITRESS_BUFFER_SIZE-1 - bufFilled, &recvSize);
		if (recvSize == 0) {
			/* connection closed too early */
			return WAITRESS_RET_CONNECTION_CLOSED;
		}
		bufFilled += recvSize;
		buf[bufFilled] = '\0';
		thisLine = buf;

		/* split */
		while (hdrParseMode != HDRM_FINISHED &&
				(nextLine = WaitressGetline (thisLine)) != NULL) {
			switch (hdrParseMode) {
				/* Status code */
				case HDRM_HEAD:
					switch (WaitressParseStatusline (thisLine)) {
						case 200:
						case 206:
							hdrParseMode = HDRM_LINES;
							break;

						case 400:
							return WAITRESS_RET_BAD_REQUEST;
							break;

						case 403:
							return WAITRESS_RET_FORBIDDEN;
							break;

						case 404:
							return WAITRESS_RET_NOTFOUND;
							break;

						case -1:
							/* ignore invalid line */
							break;

						default:
							return WAITRESS_RET_STATUS_UNKNOWN;
							break;
					}
					break;

				/* Everything else, except status code */
				case HDRM_LINES:
					/* empty line => content starts here */
					if (*thisLine == '\0') {
						hdrParseMode = HDRM_FINISHED;
					} else {
						/* parse header: "key: value", ignore invalid lines */
						char *key = thisLine, *val;

						val = strchr (thisLine, ':');
						if (val != NULL) {
							*val++ = '\0';
							while (*val != '\0' && isspace ((unsigned char) *val)) {
								++val;
							}
							WaitressHandleHeader (waith, key, val);
						}
					}
					break;

				default:
					break;
			} /* end switch */
			thisLine = nextLine;
		} /* end while strchr */
		memmove (buf, thisLine, bufFilled-(thisLine-buf));
		bufFilled -= (thisLine-buf);
	} /* end while hdrParseMode */

	*retRemaining = bufFilled;

	return wRet;
}

/*	read response header and data
 */
static WaitressReturn_t WaitressReceiveResponse (WaitressHandle_t *waith) {
	assert (waith != NULL);
	assert (waith->request.buf != NULL);

	char * const buf = waith->request.buf;
	size_t recvSize = 0;
	WaitressReturn_t wRet = WAITRESS_RET_OK;

	if ((wRet = WaitressReceiveHeaders (waith, &recvSize)) != WAITRESS_RET_OK) {
		return wRet;
	}

	do {
		/* data must be \0-terminated for chunked handler */
		buf[recvSize] = '\0';
		switch (waith->request.dataHandler (waith, buf, recvSize)) {
			case WAITRESS_HANDLER_DONE:
				return WAITRESS_RET_OK;
				break;

			case WAITRESS_HANDLER_ERR:
				return WAITRESS_RET_DECODING_ERR;
				break;

			case WAITRESS_HANDLER_ABORTED:
				return WAITRESS_RET_CB_ABORT;
				break;

			case WAITRESS_HANDLER_CONTINUE:
				/* go on */
				break;
		}
		if (waith->request.contentLengthKnown &&
				waith->request.contentReceived >= waith->request.contentLength) {
			/* don’t call read() again if we know the body’s size and have all
			 * of it already */
			break;
		}
		READ_RET (buf, WAITRESS_BUFFER_SIZE-1, &recvSize);
	} while (recvSize > 0);

	return WAITRESS_RET_OK;
}

/*	Receive data from host and call *callback ()
 *	@param waitress handle
 *	@return WaitressReturn_t
 */
WaitressReturn_t WaitressFetchCall (WaitressHandle_t *waith) {
	WaitressReturn_t wRet = WAITRESS_RET_OK;

	/* initialize */
	memset (&waith->request, 0, sizeof (waith->request));
	waith->request.sockfd = -1;
	waith->request.dataHandler = WaitressHandleIdentity;
	waith->request.read = WaitressOrdinaryRead;
	waith->request.write = WaitressOrdinaryWrite;
	waith->request.contentLengthKnown = false;

	if (waith->url.tls) {
#if defined(USE_POLARSSL)
		waith->request.sslCtx = calloc (1, sizeof(polarssl_ctx));

		entropy_init (&waith->request.sslCtx->entrophy);
		ctr_drbg_init (&waith->request.sslCtx->rnd, entropy_func, &waith->request.sslCtx->entrophy, _T("libwaitress"), 11);
		ssl_init (&waith->request.sslCtx->ssl);

		ssl_set_endpoint (&waith->request.sslCtx->ssl, SSL_IS_CLIENT);
		ssl_set_authmode (&waith->request.sslCtx->ssl, SSL_VERIFY_NONE);
		ssl_set_rng (&waith->request.sslCtx->ssl, ctr_drbg_random, &waith->request.sslCtx->rnd);
		ssl_set_session (&waith->request.sslCtx->ssl, &waith->request.sslCtx->session);
		ssl_set_bio (&waith->request.sslCtx->ssl,
			     WaitressPollRead, waith,
			     WaitressPollWrite, waith);
#else
		gnutls_init (&waith->request.tlsSession, GNUTLS_CLIENT);
		gnutls_set_default_priority (waith->request.tlsSession);

		gnutls_certificate_allocate_credentials (&waith->tlsCred);
		if (gnutls_credentials_set (waith->request.tlsSession,
				GNUTLS_CRD_CERTIFICATE,
				waith->tlsCred) != GNUTLS_E_SUCCESS) {
			return WAITRESS_RET_ERR;
		}

		/* set up custom read/write functions */
		gnutls_transport_set_ptr (waith->request.tlsSession,
				(gnutls_transport_ptr_t) waith);
		gnutls_transport_set_pull_function (waith->request.tlsSession,
				WaitressPollRead);
		gnutls_transport_set_push_function (waith->request.tlsSession,
				WaitressPollWrite);
#endif
	}

	/* buffer is required for connect already */
	waith->request.buf = malloc (WAITRESS_BUFFER_SIZE *
			sizeof (*waith->request.buf));

	/* request */
	if ((wRet = WaitressConnect (waith)) == WAITRESS_RET_OK) {
		if ((wRet = WaitressSendRequest (waith)) == WAITRESS_RET_OK) {
			wRet = WaitressReceiveResponse (waith);
		}
#if !defined(USE_POLARSSL)
		if (waith->url.tls) {
			gnutls_bye (waith->request.tlsSession, GNUTLS_SHUT_RDWR);
		}
#endif
	}

	/* cleanup */
	if (waith->url.tls) {
#if defined(USE_POLARSSL)
		ssl_free (&waith->request.sslCtx->ssl);
		free (waith->request.sslCtx);
#else
		gnutls_deinit (waith->request.tlsSession);
		gnutls_certificate_free_credentials (waith->tlsCred);
#endif
	}
	if (waith->request.sockfd != -1) {
		close (waith->request.sockfd);
	}
	free (waith->request.buf);

	if (wRet == WAITRESS_RET_OK &&
			waith->request.contentReceived < waith->request.contentLength) {
		return WAITRESS_RET_PARTIAL_FILE;
	}
	return wRet;
}

const char *WaitressErrorToStr (WaitressReturn_t wRet) {
	switch (wRet) {
		case WAITRESS_RET_OK:
			return "Everything's fine :)";
			break;

		case WAITRESS_RET_ERR:
			return "Unknown.";
			break;

		case WAITRESS_RET_STATUS_UNKNOWN:
			return "Unknown HTTP status code.";
			break;

		case WAITRESS_RET_NOTFOUND:
			return "File not found.";
			break;

		case WAITRESS_RET_FORBIDDEN:
			return "Forbidden.";
			break;

		case WAITRESS_RET_CONNECT_REFUSED:
			return "Connection refused.";
			break;

		case WAITRESS_RET_SOCK_ERR:
			return "Socket error.";
			break;

		case WAITRESS_RET_GETADDR_ERR:
			return "DNS lookup failed.";
			break;

		case WAITRESS_RET_CB_ABORT:
			return "Callback aborted request.";
			break;

		case WAITRESS_RET_PARTIAL_FILE:
			return "Partial file.";
			break;

		case WAITRESS_RET_TIMEOUT:
			return "Timeout.";
			break;

		case WAITRESS_RET_READ_ERR:
			return "Read error.";
			break;

		case WAITRESS_RET_CONNECTION_CLOSED:
			return "Connection closed by remote host.";
			break;

		case WAITRESS_RET_DECODING_ERR:
			return "Invalid encoded data.";
			break;

		case WAITRESS_RET_TLS_WRITE_ERR:
			return "TLS write failed.";
			break;

		case WAITRESS_RET_TLS_READ_ERR:
			return "TLS read failed.";
			break;

		case WAITRESS_RET_TLS_HANDSHAKE_ERR:
			return "TLS handshake failed.";
			break;

		case WAITRESS_RET_TLS_FINGERPRINT_MISMATCH:
			return "TLS fingerprint mismatch.";
			break;

		default:
			return "No error message available.";
			break;
	}
}

