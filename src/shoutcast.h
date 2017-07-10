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

#ifndef _SHOUTCAST_H
#define _SHOUTCAST_H

#include <config.h>

#include <shout/shout.h>

#include "threadqueue.h"

struct _sc_service {
	shout_t	*shout;
	pthread_t sc_thread;
	int paused;		// Sends silence
	int state;		// Thread active or idle

	// Icecast data
	char	*host;
	int	port;
	char	*user;
	char	*passwd;

	char	*mount;
	char	*bitrate;

	// buffer & message queue
	struct threadqueue sc_queue;
};

typedef struct _sc_service sc_service;

#define SCDATA		(1)
#define SCPAUSE		(2)
#define SCQUIT		(3)

#define SC_IDLE		(0)
#define SC_QUIT		(1)
#define SC_RUNNING	(2)


struct _stream_data {
	struct _stream_data *next;
	size_t len;
	unsigned char buf[];
};

typedef struct _stream_data stream_data;

extern sc_service *sc_init_service(char *server_info);
extern int sc_start_service(sc_service *svc);
extern void sc_close_service(sc_service *svc);

extern stream_data *sc_buffer_get(size_t len);
extern void sc_buffer_release(stream_data *bfr);
extern void sc_queue_add(sc_service *svc, stream_data *bfr, int mtype);

extern int sc_set_metadata(sc_service *svcr, PianoSong_t *song);

#endif /* _SHOUTCAST_H */
