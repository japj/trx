/*
 * prototyping portaudio ringbuffer with opus
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <memory.h>
#include "portaudio.h"
#include "pa_ringbuffer.h"
//#include "pa_util.h"

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

typedef struct
{
    /* Ring buffer (FIFO) for "communicating" towards audio callback */
    PaUtilRingBuffer    rBufToRT;
    void*               rBufToRTData;
} paWriteData;

/* This routine will be called by the PortAudio engine when audio is needed.
** It may called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
static int paWriteCallback(const void*                    inputBuffer,
                          void*                           outputBuffer,
                          unsigned long                   framesPerBuffer,
			              const PaStreamCallbackTimeInfo* timeInfo,
			              PaStreamCallbackFlags           statusFlags,
                          void*                           userData)
{
    int i;
    paWriteData *data = (paWriteData*)userData;
    float *out = (float*)outputBuffer;
    (void) inputBuffer; /* Prevent "unused variable" warnings. */

    /* Reset output data first */
    memset(out, 0, framesPerBuffer * 2 * sizeof(float));

#if 0

    for (i = 0; i < 16; ++i)
    {
        /* Consume the input queue */
        if (data->waves[i] == 0 && PaUtil_GetRingBufferReadAvailable(&data->rBufToRT))
        {
            OceanWave* ptr = 0;
            PaUtil_ReadRingBuffer(&data->rBufToRT, &ptr, 1);
            data->waves[i] = ptr;
        }

        if (data->waves[i] != 0)
        {
            if (GenerateWave(data->waves[i], out, framesPerBuffer))
            {
                /* If wave is "done", post it back to the main thread for deletion */
                PaUtil_WriteRingBuffer(&data->rBufFromRT, &data->waves[i], 1);
                data->waves[i] = 0;
            }
        }
    }
#endif
    return paContinue;
}

int ProtoOpenWriteStream(PaStream **stream,
		unsigned int rate, unsigned int channels, unsigned int device, paWriteData *pData)
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
							256, // frames per buffer
							paNoFlag,
							paWriteCallback,
							pData
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
    unsigned int rate = 48000;

    PaStream *outputStream;
    PaDeviceIndex outputDevice;
    unsigned int outputChannels = 2;

#if USE_INPUTSTREAM
    PaStream *inputStream;
    PaDeviceIndex inputDevice;
    unsigned int inputChannels = 1;
#endif

    paWriteData writeData = {0};
    long writeDataBufElementCount = 4096; // TODO: calculate optimal ringbuffer size
    long writeSampleSize = Pa_GetSampleSize(paFloat32);
    writeData.rBufToRTData = valloc(writeSampleSize);
    PaUtil_InitializeRingBuffer(&writeData.rBufToRT, writeSampleSize, writeDataBufElementCount, writeData.rBufToRTData);

    err = Pa_Initialize();
	if (err != paNoError)
	{
		printf("PortAudio error: %s \n", Pa_GetErrorText(err));
		return -1;
	}

    outputDevice = Pa_GetDefaultOutputDevice();


    err = ProtoOpenWriteStream(&outputStream, rate, outputChannels, outputDevice, &writeData);
    CHK("ProtoOpenWriteStream", err);

    printf("Pa_StartStream Output\n");
    err = Pa_StartStream(outputStream);
    CHK("Pa_StartStream Output", err);

#if USE_INPUTSTREAM
    inputDevice = Pa_GetDefaultInputDevice();
    err = ProtoOpenReadStream(&inputStream, rate, inputChannels, inputDevice);
    CHK("ProtoOpenReadStream", err);

    printf("Pa_StartStream Input\n");
    err = Pa_StartStream(inputStream);
    CHK("Pa_StartStream Input", err);
#endif
    while (true) {
#if USE_INPUTSTREAM
        int inputStreamActive = Pa_IsStreamActive(inputStream);
        printf("inputStreamActive: %d ", inputStreamActive);
#endif
        int outputStreamActive = Pa_IsStreamActive(outputStream);
        printf("outputStreamActive: %d", outputStreamActive);
        printf("\n");

        sleep(1);
    }

    err = Pa_Terminate();
    CHK("Pa_Terminate", err);

    return 0;
}