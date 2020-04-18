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

#include <stdio.h>
#ifdef USE_ALSA
#include <alsa/asoundlib.h>
#endif
#ifdef USE_PORTAUDIO
#include "portaudio.h"
#endif

#define CHK(call, r) { \
	if (r < 0) { \
		aerror(call, r); \
		return -1; \
	} \
}

void aerror(const char *msg, int r)
{
	fputs(msg, stderr);
	fputs(": ", stderr);
#ifdef USE_ALSA
	fputs(snd_strerror(r), stderr);
#endif
#ifdef USE_PORTAUDIO
	fputs(Pa_GetErrorText(r), stderr);
#endif
	fputc('\n', stderr);
}

#ifdef USE_ALSA
int set_alsa_hw(snd_pcm_t *pcm,
		unsigned int rate, unsigned int channels,
		unsigned int buffer)
{
	int r, dir;
	snd_pcm_hw_params_t *hw;

	snd_pcm_hw_params_alloca(&hw);

	r = snd_pcm_hw_params_any(pcm, hw);
	CHK("snd_pcm_hw_params_any", r);

	r = snd_pcm_hw_params_set_rate_resample(pcm, hw, 1);
	CHK("snd_pcm_hw_params_set_rate_resample", r);

	r = snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
	CHK("snd_pcm_hw_params_set_access", r);

	r = snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16);
	CHK("snd_pcm_hw_params_set_format", r);

	r = snd_pcm_hw_params_set_rate(pcm, hw, rate, 0);
	CHK("snd_pcm_hw_params_set_rate", r);

	r = snd_pcm_hw_params_set_channels(pcm, hw, channels);
	CHK("snd_pcm_hw_params_set_channels", r);

	dir = -1;
	r = snd_pcm_hw_params_set_buffer_time_near(pcm, hw, &buffer, &dir);
	CHK("snd_pcm_hw_params_set_buffer_time_near", r);

	r = snd_pcm_hw_params(pcm, hw);
	CHK("hw_params", r);

	return 0;
}
#endif

#ifdef USE_ALSA
int set_alsa_sw(snd_pcm_t *pcm)
{
	int r;
	snd_pcm_sw_params_t *sw;
	snd_pcm_uframes_t boundary;

	snd_pcm_sw_params_alloca(&sw);

	r = snd_pcm_sw_params_current(pcm, sw);
	CHK("snd_pcm_sw_params_current", r);

	r = snd_pcm_sw_params_get_boundary(sw, &boundary);
	CHK("snd_pcm_sw_params_get_boundary", r);

	r = snd_pcm_sw_params_set_stop_threshold(pcm, sw, boundary);
	CHK("snd_pcm_sw_params_set_stop_threshold", r);

	r = snd_pcm_sw_params(pcm, sw);
	CHK("snd_pcm_sw_params", r);

	return 0;

}
#endif

#ifdef USE_PORTAUDIO
int open_pa_writestream(PaStream **stream,
		unsigned int rate, unsigned int channels)
{
	//unsigned int framesPerBuffer;
	//framesPerBuffer = (rate / 1000) * 2;
	PaError err;
	err = Pa_OpenDefaultStream(	stream,
								channels, // 1 input channel
								0, // no output
								paInt16, // should be similar to also SND_PCM_FORMAT_S16, should also be interleaved);				
								rate,
								256, //framesPerBuffer, // frames per buffer
								NULL, // open stream in blocking mode
								NULL);
	CHK("Pa_OpenDefaultStream", err);

	return 0;
}
#endif


#ifdef USE_PORTAUDIO
int open_pa_readstream(PaStream **stream,
		unsigned int rate, unsigned int channels)
{
	//unsigned int framesPerBuffer;
	//framesPerBuffer = (rate / 1000) * 2;
	PaError err;
	err = Pa_OpenDefaultStream(	stream,
								0, // no input
								channels, // output channeles
								paInt16, // should be similar to also SND_PCM_FORMAT_S16, should also be interleaved);				
								rate,
								256, //framesPerBuffer, // frames per buffer
								NULL, // open stream in blocking mode
								NULL);
	CHK("Pa_OpenDefaultStream", err);

	return 0;
}
#endif