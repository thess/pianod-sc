/*
Copyright (c) 2015
	Ted Hess <thess@kitschensync.net>

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

/* Shoutcast client service */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "logging.h"
#include "piano.h"
#include "shoutcast.h"

struct _sc_service g_service;

static const char ourname[] = "shout";

void *sc_service_thread(void *);

// icecast buffer handling (max free list size)
#define ICY_MAX		(4)
#define ICY_BFRMAXQ	(2 * ICY_MAX)

// WAITRESS_BUFFER_SIZE + 1 MP3 frame
#define ICY_BUFSIZE	(10 * 1024 + (144 * (192000 / 44100)))


// list of available shoutcast data buffers
static stream_data *icy_head;
pthread_mutex_t icy_mutex;	// Mutex for buffer list

// count of allocated buffers (total outstanding)
static int icy_bufcnt;

// MP3 data for 0.1s of pink noise -80db (calm silence)
#include "pink_silence.h"

sc_service *sc_init_service()
{
	sc_service *svc = &g_service;

	memset(&g_service, 0, sizeof(struct _sc_service));

	shout_init();

	if ((svc->shout = shout_new()) == NULL) {
		flog(LOG_ERROR, "%s: shout_new(): %s", ourname, strerror(ENOMEM));
		return NULL;
	}

	// Startup paused
	svc->paused = 1;
	// Icecast server location
	svc->host = "localhost";
	svc->port = 6144;
	svc->mount = "/pandora";
	svc->bitrate = "192";

	// Init icecast buffer list
	pthread_mutex_init(&icy_mutex, NULL);
	icy_head = NULL;
	icy_bufcnt = 0;

	return svc;
}

void sc_close_service(sc_service *svc)
{
	void *threadRet;
	stream_data *temp;

	// Terminate shout thread
	if (svc->sc_thread) {
		// Force exit if stalled
		svc->state = SC_QUIT;
		thread_queue_add(&svc->sc_queue, NULL, SCQUIT);
		pthread_join(svc->sc_thread, &threadRet);
	}

	// Cleanup queue
	thread_queue_cleanup(&svc->sc_queue, 0);

	if (svc->shout)
		shout_free(svc->shout);

	shout_shutdown();

	pthread_mutex_lock(&icy_mutex);
	while (icy_head) {
		temp = icy_head->next;
		free(icy_head);
		icy_head = temp;
	}
	pthread_mutex_unlock(&icy_mutex);
	pthread_mutex_destroy(&icy_mutex);

	return;
}

int sc_stream_setup(sc_service *svc)
{
	shout_t *shout = svc->shout;

	if (shout_set_host(shout, svc->host) != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_host(): %s", ourname, shout_get_error(shout));
		return -1;
	}
	if (shout_set_protocol(shout, SHOUT_PROTOCOL_HTTP) != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_protocol(): %s", ourname, shout_get_error(shout));
		return -1;
	}
	if (shout_set_port(shout, svc->port) != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_port: %s", ourname, shout_get_error(shout));
		return -1;
	}
	if (shout_set_password(shout, "icymadness") != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_password(): %s", ourname, shout_get_error(shout));
		return -1;
	}
	if (shout_set_mount(shout, svc->mount) != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_mount(): %s", ourname, shout_get_error(shout));
		return -1;
	}
	if (shout_set_user(shout, "source") != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_user(): %s", ourname, shout_get_error(shout));
		return -1;
	}

	if (shout_set_format(shout, SHOUT_FORMAT_MP3) != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_format(MP3): %s", ourname, shout_get_error(shout));
		return -1;
	}

	if (shout_set_user(shout, "source") != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_user(): %s", ourname, shout_get_error(shout));
		return -1;
	}
	if (shout_set_name(shout, "PandoraRadio") != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_name(): %s", ourname, shout_get_error(shout));
		return -1;
	}
	if (shout_set_url(shout, "http://www.pandora.com") != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_url(): %s", ourname, shout_get_error(shout));
		return -1;
	}
	if (shout_set_genre(shout, "eclectic") != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_genre(): %s", ourname, shout_get_error(shout));
		return -1;
	}
	// ***TODO*** Set to Pandora station name
	if (shout_set_description(shout, "Things I listen to") != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_description(): %s", ourname, shout_get_error(shout));
		return -1;
	}
	if (shout_set_audio_info(shout, SHOUT_AI_BITRATE, svc->bitrate) != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_audio_info(AI_BITRATE): %s", ourname, shout_get_error(shout));
		return -1;
	}
	if (shout_set_audio_info(shout, SHOUT_AI_CHANNELS, "2") != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_audio_info(AI_CHANNELS): %s", ourname, shout_get_error(shout));
		return -1;
	}
	if (shout_set_audio_info(shout, SHOUT_AI_SAMPLERATE, "44100") != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_audio_info(AI_SAMPLERATE): %s", ourname, shout_get_error(shout));
		return -1;
	}

	// No-yp wanted.
	if (shout_set_public(shout, 0) != SHOUTERR_SUCCESS) {
		flog(LOG_ERROR, "%s: shout_set_public(): %s", ourname, shout_get_error(shout));
		return -1;
	}

	return 0;
}

int sc_shout_connect(sc_service *svc, int retry)
{
	flog(LOG_STATUS, "%s: Connecting to %s...", ourname, svc->host);

	while (1) {
		if (shout_open(svc->shout) == SHOUTERR_SUCCESS) {
			flog(LOG_STATUS, "%s: Connect to %s successful",
			     ourname, svc->host);
			return 0;
		}

		// re-try forever if true
		if (!retry || (svc->state == SC_QUIT))
		    break;

		// sleep then try again
		sleep(1);
	}

	flog(LOG_STATUS, "%s: Connect FAILED: %s", ourname, shout_get_error(svc->shout));

	return 1;
}

int sc_start_service(sc_service *svc)
{
	// Do nothing if already running
	if (svc->state == SC_RUNNING)
		return 0;

	// Init shout queue
	if (thread_queue_init(&svc->sc_queue)) {
		flog(LOG_ERROR, "%s: thread_queue_init() failed", ourname);
		return -1;
	}

	if (sc_stream_setup(svc)) {
		flog(LOG_ERROR, "%s: sc_stream_setup() failed", ourname);
		return -1;
	}

	// Connect to icecast and startup shout thread (paused mode)
	if (sc_shout_connect(svc, 0) == 0) {
		flog(LOG_STATUS, "%s: Connected to http://%s:%hu%s", ourname,
		       svc->host, svc->port, svc->mount);

		if (pthread_create(&svc->sc_thread, NULL, sc_service_thread, svc) != 0) {
			shout_close(svc->shout);
			return -1;
		}
		return 0;
	}

	return -1;
}

void *sc_service_thread(void *arg)
{
	sc_service *svc = (sc_service *)arg;

	struct threadmsg msg;
	struct timespec ts;
	stream_data *data;
	int ret;
	int delay;
	int retry = 0;

	flog(LOG_STATUS, "%s: sc_service_thread started", ourname);

	svc->state = SC_RUNNING;

	while(1) {
		// Check if still connected
		if (shout_get_connected(svc->shout) != SHOUTERR_CONNECTED) {
			// Handle reconnect, etc.
			flog(LOG_WARNING, "%s: Service disconnected", ourname);
			shout_close(svc->shout);
			// Reconnect (wait forever)
			sc_shout_connect(svc, 1);
			// Re-try (cleanup) queue
			continue;
		}

		if (!retry) {
			// Determine send delay
			delay = shout_delay(svc->shout);
			ts.tv_sec = 0;
			if (delay >= 1000) {
				ts.tv_sec = delay / 1000;
				delay -= ts.tv_sec * 1000;
			}
			ts.tv_nsec = delay * 1000;

			ret = thread_queue_get(&svc->sc_queue, &ts, &msg);
			if (ret == ETIMEDOUT) {
				// No data - send silence
				ret = shout_send(svc->shout, mp3_silence, mp3_silence_len);
				if (ret != SHOUTERR_SUCCESS) {
					flog(LOG_WARNING, "%s: Service disconnected", ourname);
					// Handle reconnect, etc
					shout_close(svc->shout);
					// Reconnect (wait forever)
					sc_shout_connect(svc, 1);
				}
				// Re-try msg queue
				continue;
			}
		}

		switch(msg.msgtype) {
		    case SCDATA:
			data = (stream_data *)msg.data;
			// Make sure output stream is ready
			shout_sync(svc->shout);
			ret = shout_send(svc->shout, &data->buf[0], data->len);
			if (ret != SHOUTERR_SUCCESS) {
				flog(LOG_WARNING, "%s: Service disconnected", ourname);
				// Handle reconnect, etc
				sc_shout_connect(svc, 1);
				retry = 1;
				break;
			}

			// Release data
			sc_buffer_release(data);
			retry = 0;
			break;

		    case SCQUIT:
			// cleanup and exit thread
			shout_close(svc->shout);
			svc->state = SC_IDLE;
			return 0;

		    case SCPAUSE:
			svc->paused = (svc->paused) ? 0 : 1;
			break;

		    default:
			// Error (unrecognized)
			break;
		}
	}

	svc->state = SC_IDLE;
	return 0;
}

int sc_set_metadata(sc_service *svc, PianoSong_t *song)
{
	shout_metadata_t *sc_meta;
	shout_t *shout = svc->shout;

	sc_meta = shout_metadata_new();
	if (sc_meta) {
		if (shout_metadata_add(sc_meta, "charset", "UTF-8") != SHOUTERR_SUCCESS) {
			flog(LOG_ERROR, "%s: shout_metadata_add(): %s", ourname,
					shout_get_error(shout));
		}

		if (shout_metadata_add(sc_meta, "artist", song->artist) != SHOUTERR_SUCCESS) {
			flog(LOG_ERROR, "%s: shout_metadata_add(): %s", ourname,
					shout_get_error(shout));
		}

		if (shout_metadata_add(sc_meta, "title", song->title) != SHOUTERR_SUCCESS) {
			flog(LOG_ERROR, "%s: shout_metadata_add(): %s", ourname,
					shout_get_error(shout));
		}
#if 0
		// Not used by icecast
		if (shout_metadata_add(sc_meta, "album", song->album) != SHOUTERR_SUCCESS) {
			flog(LOG_ERROR, "%s: shout_metadata_add(): %s", ourname,
					shout_get_error(shout));
		}
#endif
		if (shout_set_metadata(svc->shout, sc_meta) != SHOUTERR_SUCCESS) {
			flog(LOG_ERROR, "%s: shout_set_metadata(): %s", ourname,
					shout_get_error(shout));
		}

		shout_metadata_free(sc_meta);
	}

	return 0;
}

// Allocate a stream data buffer
stream_data *sc_buffer_get(size_t len)
{
	stream_data *newbuf;

	pthread_mutex_lock(&icy_mutex);

	// Check if too overdrawn
	if (icy_bufcnt > ICY_BFRMAXQ) {
		pthread_mutex_unlock(&icy_mutex);
		return NULL;
	}

	if (len <= ICY_BUFSIZE) {
		// Remove buffer from list?
		if (icy_head) {
			newbuf = icy_head;
			icy_head = newbuf->next;
			pthread_mutex_unlock(&icy_mutex);
		} else {
			// Alloc new buffer
			icy_bufcnt++;
			pthread_mutex_unlock(&icy_mutex);

			newbuf = (stream_data *)malloc(ICY_BUFSIZE + sizeof(struct _stream_data));
		}

		// return buffer (set size used)
		if (newbuf) {
			newbuf->len = len;
			newbuf->next = NULL;
			return newbuf;
		}

		icy_bufcnt--;
		flog(LOG_ERROR, "%s: sc_buffer_get(): %s", ourname, strerror(ENOMEM));
		return NULL;
	}

	pthread_mutex_unlock(&icy_mutex);


	// Allocate special large buffer (mark to free when done)
	newbuf = (stream_data *)malloc(len + sizeof(struct _stream_data));
	// return buffer
	if (newbuf) {
		newbuf->len = len;
		newbuf->next = (void *)0xFFFFFFFF;
		return newbuf;
	}

	flog(LOG_ERROR, "%s: sc_buffer_get(): %s", ourname, strerror(ENOMEM));
	return NULL;
}

// Release buffer (free it if list is full)
void sc_buffer_release(stream_data *bfr)
{
	stream_data *temp;

	// Check for special and free it immediately
	if (bfr->next == (void *)0xFFFFFFFF) {
		free(bfr);
		return;
	}

	pthread_mutex_lock(&icy_mutex);

	// Check for max free buffers
	if (icy_bufcnt > ICY_MAX) {
		icy_bufcnt--;

		pthread_mutex_unlock(&icy_mutex);
		// Free buffer
		free(bfr);
		return;
	}

	// Add buffer to list
	temp = icy_head;
	icy_head = bfr;
	bfr->next = temp;

	pthread_mutex_unlock(&icy_mutex);

	return;
}

// Add buffer to queue if service is connected and running
void sc_queue_add(sc_service *svc, stream_data *bfr, int mtype)
{
	// Just dump buffer if thread not running
	if (svc->state != SC_RUNNING) {
		sc_buffer_release(bfr);
		return;
	}

	thread_queue_add(&svc->sc_queue, bfr, mtype);

	return;
}
