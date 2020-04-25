#include <stdio.h>

#ifdef USE_PORTAUDIO
#include "portaudio.h"
#endif

int main(int argc, char *argv[])
{
#ifdef USE_PORTAUDIO
	PaError err;
	err = Pa_Initialize();
	if (err != paNoError)
	{
		printf("PortAudio error: %s \n", Pa_GetErrorText(err));
		return -1;
	}

	int numDevices = Pa_GetDeviceCount();
	if (numDevices < 0)
	{
		printf("ERROR: Pa_GetDeviceCount returned 0x%x\n", numDevices);
        return -1;
	}

	printf("\nPortAudio Device Information:\n");

	const PaDeviceInfo *deviceInfo;
	for (int i=0; i<numDevices;i++)
	{
		deviceInfo = Pa_GetDeviceInfo(i);
		printf("deviceId: %d (%s)\n", i, deviceInfo->name);
        printf("\tmaxInputChannels       : %d\n", deviceInfo->maxInputChannels);
        printf("\tmaxOutputChannels      : %d\n", deviceInfo->maxOutputChannels);
        printf("\tdefaultSampleRate      : %.0f\n", deviceInfo->defaultSampleRate);
        printf("\tdefaultLowInputLatency : %.0f\n", deviceInfo->defaultLowInputLatency);
        printf("\tdefaultLowOutputLatency: %.0f\n", deviceInfo->defaultLowOutputLatency);
	}

	err = Pa_Terminate();
	if (err != paNoError)
	{
		printf("PortAudio error: %s \n", Pa_GetErrorText(err));
		return -1;
	}
#endif

    return 0;
}