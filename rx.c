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
#include "payload_type_opus.h"

static unsigned int verbose = DEFAULT_VERBOSE;

static void timestamp_jump(RtpSession *session, void *a, void *b, void *c)
{
	if (verbose > 1)
		fputc('|', stderr);
	rtp_session_resync(session);
}

static RtpSession* create_rtp_recv(const char *addr_desc, const int port,
		unsigned int jitter)
{
	RtpSession *session;

	session = rtp_session_new(RTP_SESSION_RECVONLY);
	rtp_session_set_scheduling_mode(session, TRUE);
	rtp_session_set_blocking_mode(session, TRUE);
	rtp_session_set_local_addr(session, addr_desc, port, -1);
	rtp_session_set_connected_mode(session, FALSE);
	rtp_session_enable_adaptive_jitter_compensation(session, TRUE);
	rtp_session_set_jitter_compensation(session, jitter); /* ms */
	rtp_session_set_time_jump_limit(session, jitter * 16); /* ms */

	/* set the opus event payload type to 120 in the av profile.
		opusrtp defaults to sending payload type 120
	*/
	rtp_profile_set_payload(&av_profile,120,&payload_type_opus_mono);

	// payload type 11 = payload_type_l16_mono, having clock_rate of 44.1kHz, (payload info is used by jitter)
	// payload type 120 = own opus
	if (rtp_session_set_payload_type(session, 120) != 0)
		abort();
	if (rtp_session_signal_connect(session, "timestamp_jump",
					timestamp_jump, 0) != 0)
	{
		abort();
	}

	/*
	 * oRTP in RECVONLY mode attempts to send RTCP packets and
	 * segfaults (v4.3.0 tested)
	 *
	 * https://stackoverflow.com/questions/43591690/receiving-rtcp-issues-within-ortp-library
	 */

	rtp_session_enable_rtcp(session, FALSE);

	return session;
}

static int play_one_frame(void *packet,
		size_t len,
		OpusDecoder *decoder,
#ifdef USE_ALSA
		snd_pcm_t *snd,
#endif
#ifdef USE_PORTAUDIO
		PaStream *snd, // snd => stream
#endif
		const unsigned int channels)
{
	int r;
	int16_t *pcm;
	PaError err;
	// why samples = 1920? is it 2*960 (960 is max frame size opus, 2 for stereo)
#ifdef USE_ALSA
	snd_pcm_sframes_t f, samples = 1920;
#endif
#ifdef USE_PORTAUDIO
	long samples = 2880;  // 2280 is sample buffer size for decoding at at 48kHz with 60ms
						  // for better analysis of the audio I am sending with 60ms from opusrtp
#endif

	pcm = alloca(sizeof(*pcm) * samples * channels);

	if (packet == NULL) {
		r = opus_decode(decoder, NULL, 0, pcm, samples, 1);
	} else {
		r = opus_decode(decoder, packet, len, pcm, samples, 0);
	}
	if (r < 0) {
		fprintf(stderr, "opus_decode: %s\n", opus_strerror(r));
		return -1;
	}

#ifdef USE_ALSA
	f = snd_pcm_writei(snd, pcm, r);
	if (f < 0) {
		f = snd_pcm_recover(snd, f, 0);
		if (f < 0) {
			aerror("snd_pcm_writei", f);
			return -1;
		}
		return 0;
	}
	if (f < r)
		fprintf(stderr, "Short write %ld\n", f);
#endif

#ifdef USE_PORTAUDIO
	err = Pa_WriteStream(snd, pcm, r);
	if (err == paOutputUnderflowed) {
		fprintf(stderr, "Output underflowed\n");
	}
#endif

	return r;
}


static int run_rx(RtpSession *session,
		OpusDecoder *decoder,
#ifdef USE_ALSA
		snd_pcm_t *snd,
#endif
#ifdef USE_PORTAUDIO
		PaStream *snd, // snd => stream
#endif
		const unsigned int channels,
		const unsigned int rate)
{
	int ts = 0;

	struct timeval interval;
	interval.tv_sec = 0;
	interval.tv_usec = TIMED_SELECT_INTERVAL;
	
	SessionSet	*set;
	set = session_set_new();
	session_set_set(set, session);

	uint64_t tc_start, tc_now;
	tc_start = ortp_get_cur_time_ms();

	for (;;) {
		int have_more=0, packet_size, 
		decoded_size = 2880; // see also comment in play_one_frame
		unsigned char buf[32768]; 
		void *packet;

#if !USE_RECVM
		// max TIMED_SELECT_INTERVAL usec timed suspend for receiving
        //r = session_set_timedselect(set, NULL, NULL, &interval);

		packet_size = rtp_session_recv_with_ts(session, (uint8_t*)buf,
				sizeof(buf), ts, &have_more);

#else
		// recvm is recommended, but my code currently crashes so not ready yet
		mblk_t *mp = rtp_session_recvm_with_ts(session, ts);
		
		unsigned char *payload;
		packet_size = rtp_get_payload(mp, &payload);
#endif

#ifdef LINUX
		assert(packet_size >= 0);
		assert(have_more == 0);
#endif
		if (packet_size == 0) {
			packet = NULL;
			if (verbose > 1)
				fputc('#', stderr);
		} else {
#if !USE_RECVM
			packet = buf;
#else
			packet = payload;
#endif
			if (verbose > 1)
				fputc('.', stderr);
		}

		decoded_size = play_one_frame(packet, packet_size, decoder, snd, channels);
		if (decoded_size== -1)
			return -1;

		/* Follow the RFC, payload 0 has 8kHz reference rate */
		/* opusrtp does 48kHz rate, and ts follows samplecount */
		ts += decoded_size; //* 8000 / rate;
		
		// 44.1kHz rate timeclock is 2646 samples
		//ts += 2646;

		printf("play_one_frame, decoded_size:%d, packet_size: %d, ts: %d, have_more: %d\n", decoded_size, packet_size, ts, have_more);

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
	// should cleanup at the end
	session_set_destroy(set);
}

static void usage(FILE *fd)
{
	fprintf(fd, "Usage: rx [<parameters>]\n"
		"Real-time audio receiver over IP\n");

	int defaultOutput = Pa_GetDefaultOutputDevice();

	const PaDeviceInfo *deviceInfo;
	deviceInfo = Pa_GetDeviceInfo(defaultOutput);

#ifdef USE_ALSA
	fprintf(fd, "\nAudio device (ALSA) parameters:\n");
	fprintf(fd, "  -d <dev>    Device name (default '%s')\n",
		DEFAULT_DEVICE);	
#endif
#ifdef USE_PORTAUDIO
	fprintf(fd, "\nAudio device (PortAudio) parameters:\n");
	fprintf(fd, "  -d <dev>    Device id (default '%d' = '%s')\n",
		defaultOutput, deviceInfo->name);
#endif
	fprintf(fd, "  -m <ms>     Buffer time (default %d milliseconds)\n",
		DEFAULT_BUFFER);

	fprintf(fd, "\nNetwork parameters:\n");
	fprintf(fd, "  -h <addr>   IP address to listen on (default %s)\n",
		DEFAULT_ADDR);
	fprintf(fd, "  -p <port>   UDP port number (default %d)\n",
		DEFAULT_PORT);
	fprintf(fd, "  -j <ms>     Jitter buffer (default %d milliseconds)\n",
		DEFAULT_JITTER);

	fprintf(fd, "\nEncoding parameters (must match sender):\n");
	fprintf(fd, "  -r <rate>   Sample rate (default %dHz)\n",
		DEFAULT_RATE);
	fprintf(fd, "  -c <n>      Number of channels (default %d)\n",
		DEFAULT_OUTPUTCHANNELS);

	fprintf(fd, "\nProgram parameters:\n");
	fprintf(fd, "  -v <n>      Verbosity level (default %d)\n",
		DEFAULT_VERBOSE);
#ifdef LINUX
	fprintf(fd, "  -D <file>   Run as a daemon, writing process ID to the given file\n");
#endif
}

int main(int argc, char *argv[])
{
	int r, error;
#ifdef USE_ALSA
	snd_pcm_t *snd;
#endif
#ifdef USE_PORTAUDIO
	PaStream *stream;
	PaError err;
#endif
	OpusDecoder *decoder;
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
		jitter = DEFAULT_JITTER,
		channels = DEFAULT_OUTPUTCHANNELS,
	#ifdef USE_PORTAUDIO
		device = Pa_GetDefaultOutputDevice(),
	#endif
		port = DEFAULT_PORT;

	fputs(COPYRIGHT "\n", stderr);

	for (;;) {
		int c;

#ifdef LINUX
		c = getopt(argc, argv, "c:d:h:j:m:p:r:v:D");
#else
		c = getopt(argc, argv, "c:d:h:j:m:p:r:v:");
#endif
		if (c == -1)
			break;
		switch (c) {
		case 'c':
			channels = atoi(optarg);
			break;
		case 'd':
			device = atoi(optarg);
			break;
		case 'h':
			addr = optarg;
			break;
		case 'j':
			jitter = atoi(optarg);
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

	decoder = opus_decoder_create(rate, channels, &error);
	if (decoder == NULL) {
		fprintf(stderr, "opus_decoder_create: %s\n",
			opus_strerror(error));
		return -1;
	}

	ortp_init();
	ortp_scheduler_init();
	
	// enable showing of the global stats
	ortp_set_log_level_mask(NULL, ORTP_MESSAGE|ORTP_WARNING|ORTP_ERROR);
	ortp_set_log_file(stdout);

	session = create_rtp_recv(addr, port, jitter);
#ifdef LINUX
	assert(session != NULL);
#endif

#ifdef USE_ALSA
	r = snd_pcm_open(&snd, device, SND_PCM_STREAM_PLAYBACK, 0);
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
	// TODO buffer size?
	err = open_pa_writestream(&stream, rate, channels, device);
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
	r = run_rx(session, decoder, snd, channels, rate);
#endif
#ifdef USE_PORTAUDIO
	r = run_rx(session, decoder, stream, channels, rate);
#endif

#ifdef USE_PORTAUDIO
	//loop
#endif

#ifdef USE_ALSA
	if (snd_pcm_close(snd) < 0)
		abort();
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

	opus_decoder_destroy(decoder);

	return r;
}
