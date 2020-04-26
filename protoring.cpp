/*
 * prototyping portaudio ringbuffer with opus
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <memory.h>
#include <stdlib.h>
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

typedef struct
{ 
    /* Ring buffer (FIFO) for "communicating" from audio callback */
    PaUtilRingBuffer    rBufFromRT;
    void*               rBufFromRTData;
} paReadData;

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
    memset(out, 0, framesPerBuffer * 2 * sizeof(float)); // depends on channels and frameType

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
		unsigned int rate, unsigned int channels, unsigned int device, paWriteData *pWriteData)
{
	PaStreamParameters outputParameters;
    outputParameters.device = device;//Pa_GetDefaultOutputDevice();
	outputParameters.channelCount = channels;
	outputParameters.sampleFormat = paFloat32;
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
							pWriteData
	);
	CHK("Pa_OpenDefaultStream", err);

	printf("ProtoOpenWriteStream information:\n");
	log_pa_stream_info(*stream, &outputParameters);
	return 0;
}

/* This routine will be called by the PortAudio engine when audio is needed.
** It may called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
static int paReadCallback(const void*                     inputBuffer,
                          void*                           outputBuffer,
                          unsigned long                   framesPerBuffer,
			              const PaStreamCallbackTimeInfo* timeInfo,
			              PaStreamCallbackFlags           statusFlags,
                          void*                           userData)
{
    int i;
    paReadData *data = (paReadData*)userData;
    (void) outputBuffer; /* Prevent "unused variable" warnings. */

    ring_buffer_size_t availableWriteFramesInRingBuffer = PaUtil_GetRingBufferWriteAvailable(&data->rBufFromRT);
    // for now, we only write to the ring buffer if enough space is available
    if (framesPerBuffer <= availableWriteFramesInRingBuffer)
    {
        ring_buffer_size_t written = PaUtil_WriteRingBuffer(&data->rBufFromRT, inputBuffer, framesPerBuffer);
        // check if fully written?
        return paContinue;
    }

    // if we can't write data to ringbuffer, stop recording for now to early detect issues
    // TODO: determine what best to do here
    return paAbort;
}

int ProtoOpenReadStream(PaStream **stream,
        unsigned int rate, unsigned int channels, unsigned int device, paReadData *pReadData)
{
	PaStreamParameters inputParameters;
    inputParameters.device = device,
	inputParameters.channelCount = channels;
	inputParameters.sampleFormat = paFloat32;
	inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
	inputParameters.hostApiSpecificStreamInfo = NULL;

	PaError err;
	err = Pa_OpenStream(	stream,
							&inputParameters,
							NULL,
							rate,
							256,
							paNoFlag,
							paReadCallback,
							pReadData
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

    /* output stream prepare */
    PaStream *outputStream;
    PaDeviceIndex outputDevice;
    unsigned int outputChannels = 2;

    paWriteData outputData = {0};
    long writeDataBufElementCount = 4096; // TODO: calculate optimal ringbuffer size
    long writeSampleSize = Pa_GetSampleSize(paFloat32);
    outputData.rBufToRTData = valloc(writeSampleSize * writeDataBufElementCount);
    PaUtil_InitializeRingBuffer(&outputData.rBufToRT, writeSampleSize, writeDataBufElementCount, outputData.rBufToRTData);

    /* input stream prepare */
    PaStream *inputStream;
    PaDeviceIndex inputDevice;
    unsigned int inputChannels = 1;

    paReadData inputData = {0};
    long readDataBufElementCount = 4096; // TODO: calculate optimal ringbuffer size
    long readSampleSize = Pa_GetSampleSize(paFloat32);
    inputData.rBufFromRTData = valloc(readSampleSize * readDataBufElementCount);
    PaUtil_InitializeRingBuffer(&inputData.rBufFromRT, readSampleSize, readDataBufElementCount, inputData.rBufFromRTData);

    /* record/play transfer buffer */
    long transferElementCount = 4096; // TODO: calculate optimal ringbuffer size
    long transferSampleSize = Pa_GetSampleSize(paFloat32);
    void *transferBuffer = valloc(transferSampleSize * transferElementCount);

    /* PortAudio setup*/
    err = Pa_Initialize();
	if (err != paNoError)
	{
		printf("PortAudio error: %s \n", Pa_GetErrorText(err));
		return -1;
	}

    /* setup output device and stream */
    outputDevice = Pa_GetDefaultOutputDevice();
    err = ProtoOpenWriteStream(&outputStream, rate, outputChannels, outputDevice, &outputData);
    CHK("ProtoOpenWriteStream", err);

    printf("Pa_StartStream Output\n");
    err = Pa_StartStream(outputStream);
    CHK("Pa_StartStream Output", err);

    /* setup input device and stream */
    inputDevice = Pa_GetDefaultInputDevice();
    err = ProtoOpenReadStream(&inputStream, rate, inputChannels, inputDevice, &inputData);
    CHK("ProtoOpenReadStream", err);

    printf("Pa_StartStream Input\n");
    err = Pa_StartStream(inputStream);
    CHK("Pa_StartStream Input", err);

    int inputStreamActive = 1;
    int outputStreamActive = 1;
    while (inputStreamActive && outputStreamActive) {
        ring_buffer_size_t availableInReadBuffer   = PaUtil_GetRingBufferReadAvailable(&inputData.rBufFromRT);
        ring_buffer_size_t availableInWriteBuffer  = PaUtil_GetRingBufferWriteAvailable(&outputData.rBufToRT);

        inputStreamActive = Pa_IsStreamActive(inputStream);
        printf("inputStreamActive: %5d, availableInReadBuffer: %5d ", inputStreamActive, availableInReadBuffer);
        outputStreamActive = Pa_IsStreamActive(outputStream);
        printf("outputStreamActive: %5d, availableInWriteBuffer: %5d", outputStreamActive, availableInWriteBuffer);
        printf("\n");

        // transfer from recording to playback
        if (availableInWriteBuffer >= availableInReadBuffer)
        {
            ring_buffer_size_t framesWritten;
            ring_buffer_size_t framesRead;

            framesRead = PaUtil_ReadRingBuffer(&inputData.rBufFromRT, transferBuffer, availableInReadBuffer);

            framesWritten = PaUtil_WriteRingBuffer(&outputData.rBufToRT, transferBuffer, framesRead);
            printf("In->Output framesRead: %5d framesWritten: %5d", framesRead, framesWritten);
        }

        usleep(2000);
    }

    printf("Pa_StopStream Output\n");
    err = Pa_StopStream(outputStream);
    CHK("Pa_StopStream Output", err);

    printf("Pa_StopStream Input\n");
    err = Pa_StopStream(inputStream);
    CHK("Pa_StopStream Input", err);

    if (outputData.rBufToRTData) {
        free(outputData.rBufToRTData);
    }
    if (inputData.rBufFromRTData) {
        free(inputData.rBufFromRTData);
    }
    if (transferBuffer) {
        free(transferBuffer);
    }

    err = Pa_Terminate();
    CHK("Pa_Terminate", err);

    return 0;
}