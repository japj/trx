/*
 * Copyright (C) 2020 Mark Hills <mark@xwax.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#include <netdb.h>
#include <string.h>
#ifdef USE_ALSA
#include <alsa/asoundlib.h>
#endif
#ifdef USE_PORTAUDIO
#include "portaudio.h"
#endif
#include <opus/opus.h>
#include <ortp/ortp.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "defaults.h"
#include "device.h"
#include "notice.h"
#include "sched.h"

static unsigned int verbose = DEFAULT_VERBOSE;

static RtpSession* create_rtp_send(const char *addr_desc, const int port)
{
	RtpSession *session;

	session = rtp_session_new(RTP_SESSION_SENDONLY);
#ifdef LINUX
	assert(session != NULL);
#endif

	rtp_session_set_scheduling_mode(session, 0);
	rtp_session_set_blocking_mode(session, 0);
	rtp_session_set_connected_mode(session, FALSE);
	if (rtp_session_set_remote_addr(session, addr_desc, port) != 0)
		abort();
	if (rtp_session_set_payload_type(session, 0) != 0)
		abort();
	if (rtp_session_set_multicast_ttl(session, 16) != 0)
		abort();
	if (rtp_session_set_dscp(session, 40) != 0)
		abort();

	return session;
}


static int send_one_frame(
#ifdef USE_ALSA
		snd_pcm_t *snd,
#endif
#ifdef USE_PORTAUDIO
		PaStream *stream,
#endif
		const unsigned int channels,
#ifdef USE_ALSA
		const snd_pcm_uframes_t samples,
#endif
#ifdef USE_PORTAUDIO
		const long samples,
#endif
		OpusEncoder *encoder,
		const size_t bytes_per_frame,
		const unsigned int ts_per_frame,
		RtpSession *session)
{
	int16_t *pcm;
	void *packet;
	ssize_t z;
#ifdef USE_ALSA
	snd_pcm_sframes_t f;
#endif
#ifdef USE_PORTAUDIO
	PaError err;;
#endif
	static unsigned int ts = 0;

	pcm = alloca(sizeof(*pcm) * samples * channels);
	packet = alloca(bytes_per_frame);

#ifdef USE_ALSA
	f = snd_pcm_readi(snd, pcm, samples);
	if (f < 0) {
		if (f == -ESTRPIPE)
			ts = 0;

		f = snd_pcm_recover(snd, f, 0);
		if (f < 0) {
			aerror("snd_pcm_readi", f);
			return -1;
		}
		return 0;
	}
#endif
#ifdef USE_PORTAUDIO
	err = Pa_ReadStream(stream, pcm, samples);
#endif

	/* Opus encoder requires a complete frame, so if we xrun
	 * mid-frame then we discard the incomplete audio. The next
	 * read will catch the error condition and recover */
#ifdef USE_ALSA
	if (f < samples) {
		fprintf(stderr, "Short read, %ld\n", f);
		return 0;
	}
#endif
#ifdef USE_PORTAUDIO
	if (err != paNoError)
	{
		fprintf(stderr,"PortAudio error: %s \n", Pa_GetErrorText(err));
		return 0;
	}
#endif

	z = opus_encode(encoder, pcm, samples, packet, bytes_per_frame);
	if (z < 0) {
		fprintf(stderr, "opus_encode_float: %s\n", opus_strerror(z));
		return -1;
	}

	rtp_session_send_with_ts(session, packet, z, ts);
	ts += ts_per_frame;

	return 0;
}

static int run_tx(
#ifdef USE_ALSA
	    snd_pcm_t *snd,
#endif
#ifdef USE_PORTAUDIO
		PaStream *snd, // snd => stream
#endif
		const unsigned int channels,
#ifdef USE_ALSA
		const snd_pcm_uframes_t frame,
#endif
#ifdef USE_PORTAUDIO
		long frame,
#endif
		OpusEncoder *encoder,
		const size_t bytes_per_frame,
		const unsigned int ts_per_frame,
		RtpSession *session)
{
	uint64_t tc_start, tc_now;
	tc_start = ortp_get_cur_time_ms();

	for (;;) {
		int r;

		r = send_one_frame(snd, channels, frame,
				encoder, bytes_per_frame, ts_per_frame,
				session);
		if (r == -1)
			return -1;

		if (verbose > 1)
			fputc('>', stderr);

		// log globals stats on regular basis
		tc_now = ortp_get_cur_time_ms();
		if (tc_now - tc_start > STATS_INTERVAL_MS)
		{
			printf("\n\n");
			ortp_global_stats_display();
			printf("\n\n");
			tc_start = tc_now;
		}
	}
}

static void usage(FILE *fd)
{
	fprintf(fd, "Usage: tx [<parameters>]\n"
		"Real-time audio transmitter over IP\n");

	int defaultInput = Pa_GetDefaultInputDevice();

	const PaDeviceInfo *deviceInfo;
	deviceInfo = Pa_GetDeviceInfo(defaultInput);

#ifdef USE_ALSA
	fprintf(fd, "\nAudio device (ALSA) parameters:\n");
	fprintf(fd, "  -d <dev>    Device name (default '%s')\n",
		DEFAULT_DEVICE);
#endif
#ifdef USE_PORTAUDIO
	fprintf(fd, "\nAudio device (PortAudio) parameters:\n");
	fprintf(fd, "  -d <dev>    Device id (default '%d' = '%s')\n",
		defaultInput, deviceInfo->name);
#endif
	fprintf(fd, "  -m <ms>     Buffer time (default %d milliseconds)\n",
		DEFAULT_BUFFER);

	fprintf(fd, "\nNetwork parameters:\n");
	fprintf(fd, "  -h <addr>   IP address to send to (default %s)\n",
		DEFAULT_ADDR);
	fprintf(fd, "  -p <port>   UDP port number (default %d)\n",
		DEFAULT_PORT);

	fprintf(fd, "\nEncoding parameters:\n");
	fprintf(fd, "  -r <rate>   Sample rate (default %dHz)\n",
		DEFAULT_RATE);
	fprintf(fd, "  -c <n>      Number of channels (default %d)\n",
		DEFAULT_INPUTCHANNELS);
	fprintf(fd, "  -f <n>      Frame size (default %d samples, see below)\n",
		DEFAULT_FRAME);
	fprintf(fd, "  -b <kbps>   Bitrate (approx., default %d)\n",
		DEFAULT_BITRATE);

	fprintf(fd, "\nProgram parameters:\n");
	fprintf(fd, "  -v <n>      Verbosity level (default %d)\n",
		DEFAULT_VERBOSE);
#ifdef LINUX
	fprintf(fd, "  -D <file>   Run as a daemon, writing process ID to the given file\n");
#endif
	fprintf(fd, "\nAllowed frame sizes (-f) are defined by the Opus codec. For example,\n"
		"at 48000Hz the permitted values are 120, 240, 480 or 960.\n");
}

int main(int argc, char *argv[])
{
	int r, error;
	size_t bytes_per_frame;
	unsigned int ts_per_frame;
#ifdef USE_ALSA
	snd_pcm_t *snd;
#endif
#ifdef USE_PORTAUDIO
	PaStream *stream;
	PaError err;
#endif
	OpusEncoder *encoder;
	RtpSession *session;

#ifdef USE_PORTAUDIO
	err = Pa_Initialize();
	if (err != paNoError)
	{
		printf("PortAudio error: %s \n", Pa_GetErrorText(err));
		return -1;
	}
#endif


	/* command-line options */
	const char
#ifdef LINUX
		*pid = NULL,
#endif
#ifdef USE_ALSA
		*device = DEFAULT_DEVICE,
#endif
		*addr = DEFAULT_ADDR;
	unsigned int buffer = DEFAULT_BUFFER,
		rate = DEFAULT_RATE,
		channels = DEFAULT_INPUTCHANNELS,
		frame = DEFAULT_FRAME,
		kbps = DEFAULT_BITRATE,
#ifdef USE_PORTAUDIO
		device = Pa_GetDefaultInputDevice(),
#endif
		port = DEFAULT_PORT;

	fputs(COPYRIGHT "\n", stderr);

	for (;;) {
		int c;

#ifdef LINUX
		c = getopt(argc, argv, "b:c:d:f:h:m:p:r:v:D:");
#else
		c = getopt(argc, argv, "b:c:d:f:h:m:p:r:v:");
#endif
		if (c == -1)
			break;

		switch (c) {
		case 'b':
			kbps = atoi(optarg);
			break;
		case 'c':
			channels = atoi(optarg);
			break;
		case 'd':
			device = atoi(optarg);
			break;
		case 'f':
			frame = atol(optarg);
			break;
		case 'h':
			addr = optarg;
			break;
		case 'm':
			buffer = atoi(optarg);
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'r':
			rate = atoi(optarg);
			break;
		case 'v':
			verbose = atoi(optarg);
			break;
#ifdef LINUX
		case 'D':
			pid = optarg;
			break;
#endif
		default:
			usage(stderr);
			return -1;
		}
	}

	encoder = opus_encoder_create(rate, channels, OPUS_APPLICATION_AUDIO,
				&error);
	if (encoder == NULL) {
		fprintf(stderr, "opus_encoder_create: %s\n",
			opus_strerror(error));
		return -1;
	}

	bytes_per_frame = kbps * 1024 * frame / rate / 8;

	/* Follow the RFC, payload 0 has 8kHz reference rate */

	ts_per_frame = frame * 8000 / rate;

	ortp_init();
	ortp_scheduler_init();
	
	// enable showing of the global stats
	ortp_set_log_level_mask(NULL, ORTP_MESSAGE|ORTP_WARNING|ORTP_ERROR);
	ortp_set_log_file(stdout);

	session = create_rtp_send(addr, port);
#ifdef LINUX
	assert(session != NULL);
#endif

#ifdef USE_ALSA
	r = snd_pcm_open(&snd, device, SND_PCM_STREAM_CAPTURE, 0);
	if (r < 0) {
		aerror("snd_pcm_open", r);
		return -1;
	}
	if (set_alsa_hw(snd, rate, channels, buffer * 1000) == -1)
		return -1;
	if (set_alsa_sw(snd) == -1)
		return -1;
#endif

#ifdef USE_PORTAUDIO
	err = Pa_Initialize();
	if (err != paNoError)
	{
		printf("PortAudio error: %s \n", Pa_GetErrorText(err));
		return -1;
	}

	// TODO buffer size?
	err = open_pa_readstream(&stream, rate, channels, device);
	if (err != paNoError)
	{
		aerror("open_pa_stream", err);
		return -1;
	}

	err = Pa_StartStream(stream);
	if (err != paNoError)
	{
		aerror("open_pa_stream", err);
		return -1;
	}
#endif

#ifdef LINUX
	if (pid)
		go_daemon(pid);

	go_realtime();
#endif
#ifdef USE_ALSA
	r = run_tx(snd, channels, frame, encoder, bytes_per_frame,
		ts_per_frame, session);

	if (snd_pcm_close(snd) < 0)
		abort();
#endif

#ifdef USE_PORTAUDIO
	r = run_tx(stream, channels, frame, encoder, bytes_per_frame,
		ts_per_frame, session);
#endif

#ifdef USE_PORTAUDIO
	err = Pa_StopStream(stream);
	if (err != paNoError)
	{
		aerror("open_pa_stream", err);
		return -1;
	}
	
	err = Pa_CloseStream(stream);
	if (err != paNoError)
	{
		aerror("open_pa_stream", err);
		return -1;
	}

	Pa_Terminate();
#endif

	rtp_session_destroy(session);
	ortp_exit();
	ortp_global_stats_display();

	opus_encoder_destroy(encoder);

	return r;
}
