
#include <ortp/payloadtype.h>
#include <ortp/rtpprofile.h>
#include <ortp/ortp.h>
#include <ortp/rtcp.h>

char Myoffset127=127;
char Myoffset0xD5=(char)0xD5;
char Myoffset0[4] = {0x00, 0x00, 0x00, 0x00};

/*
 * IMPORTANT : some compiler don't support the tagged-field syntax. Those
 * macros are there to trap the problem This means that if you want to keep
 * portability, payload types must be defined with their fields in the right
 * order.
 */
#if defined(_ISOC99_SOURCE) || defined(__clang__)
// ISO C99's tagged syntax
#define TYPE(val)		.type=(val)
#define CLOCK_RATE(val)		.clock_rate=(val)
#define BITS_PER_SAMPLE(val)	.bits_per_sample=(val)
#define ZERO_PATTERN(val)	.zero_pattern=(val)
#define PATTERN_LENGTH(val)	.pattern_length=(val)
#define NORMAL_BITRATE(val)	.normal_bitrate=(val)
#define MIME_TYPE(val)		.mime_type=(val)
#define CHANNELS(val)		.channels=(val)
#define RECV_FMTP(val)		.recv_fmtp=(val)
#define SEND_FMTP(val)		.send_fmtp=(val)
#define NO_AVPF		.avpf={.features=PAYLOAD_TYPE_AVPF_NONE, .trr_interval=0}
#define AVPF(feat, intv)		.avpf={.features=(feat), .trr_interval=(intv)}
#define FLAGS(val)		.flags=(val)
#elif defined(__GNUC__)
// GCC's legacy tagged syntax (even old versions have it)
#define TYPE(val)		type: (val)
#define CLOCK_RATE(val)		clock_rate: (val)
#define BITS_PER_SAMPLE(val)	bits_per_sample: (val)
#define ZERO_PATTERN(val)	zero_pattern: (val)
#define PATTERN_LENGTH(val)	pattern_length: (val)
#define NORMAL_BITRATE(val)	normal_bitrate: (val)
#define MIME_TYPE(val)		mime_type: (val)
#define CHANNELS(val)		channels: (val)
#define RECV_FMTP(val)		recv_fmtp: (val)
#define SEND_FMTP(val)		send_fmtp: (val)
#define NO_AVPF		avpf: {features: PAYLOAD_TYPE_AVPF_NONE, trr_interval: 0}
#define AVPF(feat, intv)		avpf: {features: (feat), trr_interval: (intv)}
#define FLAGS(val)		flags: (val)
#else
// No tagged syntax supported
#define TYPE(val)		(val)
#define CLOCK_RATE(val)		(val)
#define BITS_PER_SAMPLE(val)	(val)
#define ZERO_PATTERN(val)	(val)
#define PATTERN_LENGTH(val)	(val)
#define NORMAL_BITRATE(val)	(val)
#define MIME_TYPE(val)		(val)
#define CHANNELS(val)		(val)
#define RECV_FMTP(val)		(val)
#define SEND_FMTP(val)		(val)
#define NO_AVPF		{PAYLOAD_TYPE_AVPF_NONE, 0}
#define AVPF(feat, intv)		{(feat), FALSE, (intv)}
#define FLAGS(val)		(val)

#endif

// borrowed the macros from ortp/avprofile.c

PayloadType payload_type_opus_mono={
	TYPE(PAYLOAD_AUDIO_CONTINUOUS),
	CLOCK_RATE(48000),
	BITS_PER_SAMPLE(16),
	ZERO_PATTERN(Myoffset0),
	PATTERN_LENGTH(2),
	NORMAL_BITRATE(768000),				/* (48000 x 16bits per frame x 1 channel) */
	MIME_TYPE("OPUSMONO"),
	CHANNELS(1),
	RECV_FMTP(NULL),
	SEND_FMTP(NULL),
	NO_AVPF,
	FLAGS(0)
};