/*
Copyright (c) 2008-2013
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

/* receive/play audio stream */

#include <config.h>

#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include "player.h"

#define bigToHostEndian32(x) ntohl(x)

#if defined(ENABLE_CAPTURE)
void ripit_open_file(struct audioPlayer *player, PianoSong_t *song);
void ripit_close_file(struct audioPlayer *player);
void ripit_write_stream(struct audioPlayer *player);
#endif

/* pandora uses float values with 2 digits precision. Scale them by 100 to get
 * a "nice" integer */
#define RG_SCALE_FACTOR 100

/*	wait until the pause flag is cleared
 *	@param player structure
 *	@return true if the player should quit
 */
static bool BarPlayerCheckPauseQuit (struct audioPlayer *player) {
	bool quit = false;

	pthread_mutex_lock (&player->pauseMutex);
	while (true) {
		if (player->doQuit) {
			/* pianod tweak: Don't quit before player is fully initialized. */
			if (player->mode >= PLAYER_SAMPLESIZE_INITIALIZED) {
				quit = true;
			}
			break;
		}
		if (!player->doPause) {
			break;
		}
		pthread_cond_wait(&player->pauseCond,
				  &player->pauseMutex);
	}
	pthread_mutex_unlock (&player->pauseMutex);

	return quit;
}

/*	compute replaygain scale factor
 *	algo taken from here: http://www.dsprelated.com/showmessage/29246/1.php
 *	mpd does the same
 *	@param apply this gain
 *	@return this * yourvalue = newgain value
 */
unsigned int BarPlayerCalcScale (const float applyGain) {
	return pow(10.0, applyGain / 20.0) * RG_SCALE_FACTOR;
}

/*	apply replaygain to signed short value
 *	@param value
 *	@param replaygain scale (calculated by computeReplayGainScale)
 *	@return scaled value
 */
static inline signed short int applyReplayGain (const signed short int value,
		const unsigned int scale) {
	int tmpReplayBuf = value * scale;
	/* avoid clipping */
	if (tmpReplayBuf > SHRT_MAX*RG_SCALE_FACTOR) {
		return SHRT_MAX;
	} else if (tmpReplayBuf < SHRT_MIN*RG_SCALE_FACTOR) {
		return SHRT_MIN;
	} else {
		return tmpReplayBuf / RG_SCALE_FACTOR;
	}
}

/*	Refill player's buffer with dataSize of data
 *	@param player structure
 *	@param new data
 *	@param data size
 *	@return 1 on success, 0 when buffer overflow occured
 */
static inline int BarPlayerBufferFill (struct audioPlayer *player,
		const char *data, const size_t dataSize) {
	/* fill buffer */
	if (player->bufferFilled + dataSize > BAR_PLAYER_BUFSIZE) {
		BarUiMsg (player->settings, MSG_ERR, "Buffer overflow!\n");
		return 0;
	}
	memcpy (player->buffer+player->bufferFilled, data, dataSize);
	player->bufferFilled += dataSize;
	player->bufferRead = 0;
	player->bytesReceived += dataSize;
	return 1;
}

/*	move data beginning from read pointer to buffer beginning and
 *	overwrite data already read from buffer
 *	@param player structure
 *	@return nothing at all
 */
static inline void BarPlayerBufferMove (struct audioPlayer *player) {
	/* move remaining bytes to buffer beginning */
	memmove (player->buffer, player->buffer + player->bufferRead,
			(player->bufferFilled - player->bufferRead));
	player->bufferFilled -= player->bufferRead;
}

/*	open audio output stream, passing output parameters to libao.
 *	@param player data structure
 *	@return audio output device from libao (NULL on failure)
 */
static ao_device *BarPlayerOpenAudioOut (struct audioPlayer *player) {
	int audioOutDriver = -1;

	/* Find driver, or use default if unspecified. */
	audioOutDriver = player->driver ? ao_driver_id (player->driver)
								    : ao_default_driver_id();
	if (audioOutDriver < 0) {
		BarUiMsg (player->settings, MSG_ERR,
				  "audio driver '%s' not found\n",
				  player->driver ? player->driver : "(default)");
		return NULL;
	}

	/* Audio format structure for libao */
	ao_sample_format format;
	memset (&format, 0, sizeof (format));
	format.bits = 16;
	format.channels = player->channels;
	format.rate = player->samplerate;
	format.byte_format = AO_FMT_NATIVE;

	/* Create a list of ao_options */
	ao_option *options = NULL;
	ao_append_option(&options, "client_name", PACKAGE);
	if (player->device) {
		ao_append_option (&options, "dev", player->device);
	}
	if (player->id) {
		ao_append_option (&options, "id", player->id);
	}
	if (player->server) {
		ao_append_option (&options, "server", player->server);
	}

	ao_device *dev = ao_open_live (audioOutDriver, &format, options);
	if (dev == NULL) {
		BarUiMsg (player->settings, MSG_ERR,
				  "Cannot open audio device %s/%s/%s, trying default\n",
				  player->device ? player->device : "default",
				  player->id ? player->id : "default",
				  player->server ? player->server : "default");
		dev = ao_open_live (audioOutDriver, &format, NULL);
	}
	ao_free_options (options);
	return dev;
}

#ifdef ENABLE_FAAD

/*	play aac stream
 *	@param streamed data
 *	@param received bytes
 *	@param extra data (player data)
 *	@return received bytes or less on error
 */
static WaitressCbReturn_t BarPlayerAACCb (void *ptr, size_t size,
		void *stream) {
	const char *data = ptr;
	struct audioPlayer *player = stream;

	if (BarPlayerCheckPauseQuit (player) ||
			!BarPlayerBufferFill (player, data, size)) {
		return WAITRESS_CB_RET_ERR;
	}

	if (player->mode == PLAYER_RECV_DATA) {
		short int *aacDecoded;
		NeAACDecFrameInfo frameInfo;
		size_t i;

		while (player->sampleSizeCurr < player->sampleSizeN &&
				(player->bufferFilled - player->bufferRead) >=
			player->sampleSize[player->sampleSizeCurr]) {
			/* going through this loop can take up to a few seconds =>
			 * allow earlier thread abort */
			if (BarPlayerCheckPauseQuit (player)) {
				return WAITRESS_CB_RET_ERR;
			}

			/* decode frame */
			aacDecoded = NeAACDecDecode(player->aacHandle, &frameInfo,
					&player->buffer[player->bufferRead],
					player->sampleSize[player->sampleSizeCurr]);
			player->bufferRead += player->sampleSize[player->sampleSizeCurr];
			++player->sampleSizeCurr;

			if (frameInfo.error != 0) {
				/* skip this frame, songPlayed will be slightly off if this
				 * happens */
				BarUiMsg (player->settings, MSG_ERR, "Decoding error: %s\n",
						NeAACDecGetErrorMessage (frameInfo.error));
				continue;
			}
			/* assuming data in stsz atom is correct */
			assert (frameInfo.bytesconsumed ==
					player->sampleSize[player->sampleSizeCurr-1]);

			for (i = 0; i < frameInfo.samples; i++) {
				aacDecoded[i] = applyReplayGain (aacDecoded[i], player->scale);
			}
			/* ao_play needs bytes: 1 sample = 16 bits = 2 bytes */
			ao_play (player->audioOutDevice, (char *) aacDecoded,
					frameInfo.samples * 2);
			/* add played frame length to played time, explained below */
			player->songPlayed += (unsigned long long int) frameInfo.samples *
					(unsigned long long int) BAR_PLAYER_MS_TO_S_FACTOR /
					(unsigned long long int) player->samplerate /
					(unsigned long long int) player->channels;
		}
		if (player->sampleSizeCurr >= player->sampleSizeN) {
			/* no more frames, drop data */
			player->bufferRead = player->bufferFilled;
		}
	} else {
		if (player->mode == PLAYER_INITIALIZED) {
			while (player->bufferRead+4 < player->bufferFilled) {
				if (memcmp (player->buffer + player->bufferRead, "esds",
						4) == 0) {
					player->mode = PLAYER_FOUND_ESDS;
					player->bufferRead += 4;
					break;
				}
				player->bufferRead++;
			}
		}
		if (player->mode == PLAYER_FOUND_ESDS) {
			/* FIXME: is this the correct way? */
			/* we're gonna read 10 bytes */
			while (player->bufferRead+1+4+5 < player->bufferFilled) {
				if (memcmp (player->buffer + player->bufferRead,
						"\x05\x80\x80\x80", 4) == 0) {
					/* +1+4 needs to be replaced by <something>! */
					player->bufferRead += 1+4;
					char err = NeAACDecInit2 (player->aacHandle, player->buffer +
							player->bufferRead, 5, &player->samplerate,
							&player->channels);
					player->bufferRead += 5;
					if (err != 0) {
						BarUiMsg (player->settings, MSG_ERR,
								"Error while initializing audio decoder "
								"(%i)\n", err);
						return WAITRESS_CB_RET_ERR;
					}

					if ((player->audioOutDevice = BarPlayerOpenAudioOut (player)) == NULL) {
						/* we're not interested in the errno */
						player->aoError = 1;
						BarUiMsg (player->settings, MSG_ERR,
								"Cannot open audio device\n");
						return WAITRESS_CB_RET_ERR;
					}
					player->mode = PLAYER_AUDIO_INITIALIZED;
					break;
				}
				player->bufferRead++;
			}
		}
		if (player->mode == PLAYER_AUDIO_INITIALIZED) {
			while (player->bufferRead+4+8 < player->bufferFilled) {
				if (memcmp (player->buffer + player->bufferRead, "stsz",
						4) == 0) {
					player->mode = PLAYER_FOUND_STSZ;
					player->bufferRead += 4;
					/* skip version and unknown */
					player->bufferRead += 8;
					break;
				}
				player->bufferRead++;
			}
		}
		/* get frame sizes */
		if (player->mode == PLAYER_FOUND_STSZ) {
			while (player->bufferRead+4 < player->bufferFilled) {
				/* how many frames do we have? */
				if (player->sampleSizeN == 0) {
					/* mp4 uses big endian, convert */
					memcpy (&player->sampleSizeN, player->buffer +
							player->bufferRead, sizeof (uint32_t));
					player->sampleSizeN =
							bigToHostEndian32 (player->sampleSizeN);

					player->sampleSize = malloc (player->sampleSizeN *
							sizeof (*player->sampleSize));
					assert (player->sampleSize != NULL);
					player->bufferRead += sizeof (uint32_t);
					player->sampleSizeCurr = 0;
					/* set up song duration (assuming one frame always contains
					 * the same number of samples)
					 * calculation: channels * number of frames * samples per
					 * frame / samplerate */
					/* FIXME: Hard-coded number of samples per frame */
					player->songDuration = (unsigned long long int) player->sampleSizeN *
							4096LL * (unsigned long long int) BAR_PLAYER_MS_TO_S_FACTOR /
							(unsigned long long int) player->samplerate /
							(unsigned long long int) player->channels;
					break;
				} else {
					memcpy (&player->sampleSize[player->sampleSizeCurr],
							player->buffer + player->bufferRead,
							sizeof (uint32_t));
					player->sampleSize[player->sampleSizeCurr] =
							bigToHostEndian32 (
							player->sampleSize[player->sampleSizeCurr]);

					player->sampleSizeCurr++;
					player->bufferRead += sizeof (uint32_t);
				}
				/* all sizes read, nearly ready for data mode */
				if (player->sampleSizeCurr >= player->sampleSizeN) {
					player->mode = PLAYER_SAMPLESIZE_INITIALIZED;
					break;
				}
			}
		}
		/* search for data atom and let the show begin... */
		if (player->mode == PLAYER_SAMPLESIZE_INITIALIZED) {
			while (player->bufferRead+4 < player->bufferFilled) {
				if (memcmp (player->buffer + player->bufferRead, "mdat",
						4) == 0) {
					player->mode = PLAYER_RECV_DATA;
					player->sampleSizeCurr = 0;
					player->bufferRead += 4;
					break;
				}
				player->bufferRead++;
			}
		}
	}

#if defined(ENABLE_CAPTURE)
	/* Dump decoded data to file */
	ripit_write_stream(player);
#endif

	BarPlayerBufferMove (player);

	return WAITRESS_CB_RET_OK;
}

#endif /* ENABLE_FAAD */

#ifdef ENABLE_MAD

/*	convert mad's internal fixed point format to short int
 *	@param mad fixed
 *	@return short int
 */
static inline signed short int BarPlayerMadToShort (const mad_fixed_t fixed) {
	/* Clipping */
	if (fixed >= MAD_F_ONE) {
		return SHRT_MAX;
	} else if (fixed <= -MAD_F_ONE) {
		return -SHRT_MAX;
	}

	/* Conversion */
	return (signed short int) (fixed >> (MAD_F_FRACBITS - 15));
}

/*	mp3 playback callback
 */
static WaitressCbReturn_t BarPlayerMp3Cb (void *ptr, size_t size,
		void *stream) {
	const char *data = ptr;
	struct audioPlayer *player = stream;
	size_t i;
#if defined(ENABLE_SHOUT)
	stream_data *sdata;
#endif

	if (BarPlayerCheckPauseQuit (player) ||
			!BarPlayerBufferFill (player, data, size)) {
		return WAITRESS_CB_RET_ERR;
	}

	/* some "prebuffering" */
	if (player->mode < PLAYER_RECV_DATA &&
			player->bufferFilled < BAR_PLAYER_BUFSIZE / 2) {
		return WAITRESS_CB_RET_OK;
	}

	mad_stream_buffer (&player->mp3Stream, player->buffer,
			player->bufferFilled);
	player->mp3Stream.error = MAD_ERROR_NONE;
	do {
		/* channels * max samples, found in mad.h */
		signed short int *madPtr = player->madDecoded;

		if (mad_frame_decode (&player->mp3Frame, &player->mp3Stream) != 0) {
			if (player->mp3Stream.error != MAD_ERROR_BUFLEN) {
				if (player->mp3Stream.error == MAD_ERROR_LOSTSYNC) {
					// Attempt to re-sync stream (and continue)
					if (mad_stream_sync(&player->mp3Stream) == 0) {
						player->mp3Stream.error = MAD_ERROR_NONE;
						continue;
					}
					// Stream sync error and exit player
					BarUiMsg(player->settings, MSG_ERR,
						 "mp3 stream re-sync failed\n");
					return WAITRESS_CB_RET_ERR;
				}
				BarUiMsg (player->settings, MSG_ERR,
						"mp3 decoding error: %s\n",
						mad_stream_errorstr (&player->mp3Stream));
				return WAITRESS_CB_RET_ERR;
			} else {
				/* rebuffering required => exit loop */
				break;
			}
		}
		mad_synth_frame (&player->mp3Synth, &player->mp3Frame);
		for (i = 0; i < player->mp3Synth.pcm.length; i++) {
			/* left channel */
			*(madPtr++) = applyReplayGain (BarPlayerMadToShort (
					player->mp3Synth.pcm.samples[0][i]), player->scale);

			/* right channel */
			*(madPtr++) = applyReplayGain (BarPlayerMadToShort (
					player->mp3Synth.pcm.samples[1][i]), player->scale);
		}
		if (player->mode < PLAYER_AUDIO_INITIALIZED) {
			player->channels = player->mp3Synth.pcm.channels;
			player->samplerate = player->mp3Synth.pcm.samplerate;
			if ((player->audioOutDevice = BarPlayerOpenAudioOut (player)) == NULL) {
				player->aoError = 1;
				BarUiMsg (player->settings, MSG_ERR,
						"Cannot open audio device\n");
				return WAITRESS_CB_RET_ERR;
			}

			/* calc song length using the framerate of the first decoded frame */
			player->songDuration = (unsigned long long int) player->waith.request.contentLength /
					((unsigned long long int) player->mp3Frame.header.bitrate /
					(unsigned long long int) BAR_PLAYER_MS_TO_S_FACTOR / 8LL);

			/* must be > PLAYER_SAMPLESIZE_INITIALIZED, otherwise time won't
			 * be visible to user (ugly, but mp3 decoding != aac decoding) */
			player->mode = PLAYER_RECV_DATA;
		}
		/* samples * length * channels */
		ao_play (player->audioOutDevice, (char *) player->madDecoded,
				player->mp3Synth.pcm.length * 2 * 2);

		/* avoid division by 0 */
		if (player->mode == PLAYER_RECV_DATA) {
			/* same calculation as in aac player; don't need to divide by
			 * channels, length is number of samples for _one_ channel */
			player->songPlayed += (unsigned long long int) player->mp3Synth.pcm.length *
					(unsigned long long int) BAR_PLAYER_MS_TO_S_FACTOR /
					(unsigned long long int) player->samplerate;
		}

		if (BarPlayerCheckPauseQuit (player)) {
			return WAITRESS_CB_RET_ERR;
		}
	} while (player->mp3Stream.error != MAD_ERROR_BUFLEN);

	player->bufferRead += player->mp3Stream.next_frame - player->buffer;

#if defined(ENABLE_SHOUT)
	// send raw mp3 data to icecast server
	if (player->shoutcast) {
		sdata = sc_buffer_get(player->bufferRead);
		if (sdata) {
			memcpy(&sdata->buf[0], player->buffer, player->bufferRead);
			sc_queue_add(player->shoutcast, sdata, SCDATA);
		}
	}
#endif

#if defined(ENABLE_CAPTURE)
	/* Dump stream data to file */
	ripit_write_stream(player);
#endif

	BarPlayerBufferMove (player);

	return WAITRESS_CB_RET_OK;
}
#endif /* ENABLE_MAD */

/*	player thread; for every song a new thread is started
 *	@param audioPlayer structure
 *	@return PLAYER_RET_*
 */
void *BarPlayerThread (void *data) {
	struct audioPlayer *player = data;
	char extraHeaders[32];
	void *ret = PLAYER_RET_OK;
	#ifdef ENABLE_FAAD
	NeAACDecConfigurationPtr conf;
	#endif
	WaitressReturn_t wRet = WAITRESS_RET_ERR;

	/* init handles */
	player->waith.data = (void *) player;
	/* extraHeaders will be initialized later */
	player->waith.extraHeaders = extraHeaders;
	player->buffer = malloc (BAR_PLAYER_BUFSIZE);

	switch (player->audioFormat) {
		#ifdef ENABLE_FAAD
		case PIANO_AF_AACPLUS:
			player->aacHandle = NeAACDecOpen();
			/* set aac conf */
			conf = NeAACDecGetCurrentConfiguration(player->aacHandle);
			conf->outputFormat = FAAD_FMT_16BIT;
		    conf->downMatrix = 1;
			NeAACDecSetConfiguration(player->aacHandle, conf);

			player->waith.callback = BarPlayerAACCb;
			break;
		#endif /* ENABLE_FAAD */

		#ifdef ENABLE_MAD
		case PIANO_AF_MP3:
			mad_stream_init (&player->mp3Stream);
			mad_frame_init (&player->mp3Frame);
			mad_synth_init (&player->mp3Synth);
			player->madDecoded = malloc (2 * 1152 * sizeof(short int));
			player->waith.callback = BarPlayerMp3Cb;
			break;
		#endif /* ENABLE_MAD */

		default:
			BarUiMsg (player->settings, MSG_ERR, "Unsupported audio format!\n");
			ret = (void *) PLAYER_RET_HARDFAIL;
			goto cleanup;
			break;
	}

	player->mode = PLAYER_INITIALIZED;

	/* This loop should work around song abortions by requesting the
	 * missing part of the song */
	do {
		snprintf (extraHeaders, sizeof (extraHeaders), "Range: bytes=%zu-\r\n",
				player->bytesReceived);
		wRet = WaitressFetchCall (&player->waith);
	} while (wRet == WAITRESS_RET_PARTIAL_FILE || wRet == WAITRESS_RET_TIMEOUT
			|| wRet == WAITRESS_RET_READ_ERR);

	switch (player->audioFormat) {
		#ifdef ENABLE_FAAD
		case PIANO_AF_AACPLUS:
			NeAACDecClose(player->aacHandle);
			free (player->sampleSize);
			break;
		#endif /* ENABLE_FAAD */

		#ifdef ENABLE_MAD
		case PIANO_AF_MP3:
			mad_synth_finish (&player->mp3Synth);
			mad_frame_finish (&player->mp3Frame);
			mad_stream_finish (&player->mp3Stream);
			if (player->madDecoded)
				free(player->madDecoded);
			player->madDecoded = NULL;
			break;
		#endif /* ENABLE_MAD */

		default:
			/* this should never happen */
			assert (0);
			break;
	}

#if defined(ENABLE_CAPTURE)
	/* Close stream capture */
	ripit_close_file(player);
#endif

	if (player->aoError) {
		ret = (void *) PLAYER_RET_HARDFAIL;
	}

	/* Pandora sends broken audio url’s sometimes (“bad request”). ignore them. */
	if (wRet != WAITRESS_RET_OK && wRet != WAITRESS_RET_CB_ABORT) {
		BarUiMsg (player->settings, MSG_ERR, "Cannot access audio file: %s\n",
				WaitressErrorToStr (wRet));
		ret = (void *) PLAYER_RET_SOFTFAIL;
	}

	ao_close(player->audioOutDevice);
cleanup:
	WaitressFree (&player->waith);
	free (player->buffer);

	player->mode = PLAYER_FINISHED_PLAYBACK;

	return ret;
}

#if defined(ENABLE_CAPTURE)

/***************
 * Stream ripper
 ***************/

/* strcat and fixup legal file name */
char *ripit_normalize_strcat(char *fname, char *str)
{
	char *iptr = str;
	char *optr = fname + strlen(fname);
	char ch;

	while ((ch = *iptr++))
	{
		switch(ch)
		{
		case '<':
			ch = '[';
			break;
		case '>':
			ch = ']';
			break;
		case ':':
			ch = ';';
			break;
		case '"':
			ch = '\'';
			break;
		case '*':
		case '?':
			ch = '!';
			break;
		case '/':
		case '\\':
		case '|':
			ch = '_';
			break;
		}
		*optr++ = ch;
	}

	*optr = '\0';

	return fname;
}

void ripit_open_file(struct audioPlayer *player, PianoSong_t *song)
{
	int namelen;
	char *file_name;

    // Safety cleanup
    if (player->ripit_file)
		ripit_close_file(player);

	namelen = player->settings->capture_pathlen;
	namelen += strlen(song->artist);
	namelen += strlen(song->title);
	file_name = malloc(namelen + 4 + 3 + 2); /* len + filetype + punctuation + pad */

	/* Fill it in */
	strcpy(file_name, player->settings->capture_path);
	if (file_name[player->settings->capture_pathlen - 1] != '/') {
		file_name[player->settings->capture_pathlen] = '/';
		file_name[player->settings->capture_pathlen + 1] = '\0';
	}

	ripit_normalize_strcat(file_name, song->artist);
	strcat(file_name, " - ");
	ripit_normalize_strcat(file_name, song->title);
	strcat(file_name, (player->audioFormat == PIANO_AF_AACPLUS) ? ".aac" : ".mp3");

	player->ripit_file = fopen(file_name, "wb");
	chmod(file_name, 0664);
	if (!player->ripit_file) {
		flog(LOG_ERROR, "Capture file open failed(%d): %s", errno, strerror(errno));
	}

    player->ripit_fname = file_name;

	return;
}

void ripit_close_file(struct audioPlayer *player)
{
    struct stat sbuf;

	if (player->ripit_file) {
        // Check for 0-length files and remove them
        stat(player->ripit_fname, &sbuf);
        if (sbuf.st_size == 0) {
            unlink(player->ripit_fname);
        }
        // close file & release name
		fclose(player->ripit_file);
		free(player->ripit_fname);
	}

	player->ripit_file = NULL;
    player->ripit_fname = NULL;

    return;
}

void ripit_write_stream(struct audioPlayer *player)
{
	if (player->ripit_file) {
		fwrite(player->buffer, sizeof(char),
		       player->bufferRead,
		       player->ripit_file);
	}
}

#endif	/* ENABLE_CAPTURE */
