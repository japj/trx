/*
 * prototyping portaudio ringbuffer with opus
 */
#include <stdio.h>
#include <unistd.h>
#include "portaudio.h"

#define CHK(call, r) { \
	if (r < 0) { \
		paerror(call, r); \
		return -1; \
	} \
}

void paerror(const char *msg, int r)
{
	fputs(msg, stderr);
	fputs(": ", stderr);
	fputs(Pa_GetErrorText(r), stderr);
	fputc('\n', stderr);
}

void log_pa_stream_info(PaStream *stream, PaStreamParameters *params)
{
	const PaDeviceInfo *deviceInfo;
	deviceInfo = Pa_GetDeviceInfo(params->device);
	const PaStreamInfo *streamInfo;
	streamInfo = Pa_GetStreamInfo(stream);

	printf("DeviceId:         %d (%s)\n", params->device, deviceInfo->name);
	printf("ChannelCount:     %d\n", params->channelCount);
	printf("SuggestedLatency: %f\n", params->suggestedLatency);
	printf("InputLatency:     %f\n", streamInfo->inputLatency);
	printf("OutputLatency:    %f\n", streamInfo->outputLatency);
	printf("SampleRate:       %.f\n", streamInfo->sampleRate);
}

int ProtoOpenWriteStream(PaStream **stream,
		unsigned int rate, unsigned int channels, unsigned int device)
{
	PaStreamParameters outputParameters;
    outputParameters.device = device;//Pa_GetDefaultOutputDevice();
	outputParameters.channelCount = channels;
	outputParameters.sampleFormat = paInt16;
	outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
	outputParameters.hostApiSpecificStreamInfo = NULL;

	PaError err;
	err = Pa_OpenStream(	stream,
							NULL,
							&outputParameters,
							rate,
							256,
							paNoFlag,
							NULL,
							NULL
	);
	CHK("Pa_OpenDefaultStream", err);

	printf("ProtoOpenWriteStream information:\n");
	log_pa_stream_info(*stream, &outputParameters);
	return 0;
}

int ProtoOpenReadStream(PaStream **stream,
        unsigned int rate, unsigned int channels, unsigned int device)
{
	PaStreamParameters inputParameters;
    inputParameters.device = device,
	inputParameters.channelCount = channels;
	inputParameters.sampleFormat = paInt16;
	inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
	inputParameters.hostApiSpecificStreamInfo = NULL;

	PaError err;
	err = Pa_OpenStream(	stream,
							&inputParameters,
							NULL,
							rate,
							256,
							paNoFlag,
							NULL,
							NULL
	);
	CHK("ProtoOpenReadStream", err);

	printf("ProtoOpenReadStream information:\n");
	log_pa_stream_info(*stream, &inputParameters);
	return 0;
}

int main(int argc, char *argv[])
{
	PaError err;
    PaStream *outputStream;
    PaStream *inputStream;
    PaDeviceIndex outputDevice;
    PaDeviceIndex inputDevice;

    unsigned int rate = 48000;
    unsigned int inputChannels = 1;
    unsigned int outputChannels = 2;

    err = Pa_Initialize();
	if (err != paNoError)
	{
		printf("PortAudio error: %s \n", Pa_GetErrorText(err));
		return -1;
	}

    outputDevice = Pa_GetDefaultOutputDevice();
    inputDevice = Pa_GetDefaultInputDevice();

    err = ProtoOpenWriteStream(&outputStream, rate, outputChannels, outputDevice);
    CHK("ProtoOpenWriteStream", err);

    err = ProtoOpenReadStream(&inputStream, rate, inputChannels, inputDevice);
    CHK("ProtoOpenReadStream", err);
    
    printf("Pa_StartStream Output\n");
    err = Pa_StartStream(outputStream);
    CHK("Pa_StartStream Output", err);

    printf("Pa_StartStream Input\n");
    err = Pa_StartStream(inputStream);
    CHK("Pa_StartStream Input", err);

    while (true) {
        int inputStreamActive = Pa_IsStreamActive(inputStream);
        int outputStreamActive = Pa_IsStreamActive(outputStream);
        printf("inputStreamActive: %d, outputStreamActive: %d", inputStreamActive, outputStreamActive);
        sleep(1);
    }

    err = Pa_Terminate();
    CHK("Pa_Terminate", err);

    return 0;
}