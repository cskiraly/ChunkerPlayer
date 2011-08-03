#ifndef _CHUNKER_PLAYER_CORE_H
#define _CHUNKER_PLAYER_CORE_H

#include <stdint.h>

typedef struct threadVal {
	int width;
	int height;
	char *video_codec;
} ThreadVal;

int ChunkerPlayerCore_InitCodecs(char *v_codec, int width, int height, char *audio_codec, int sample_rate, short int audio_channels);
int ChunkerPlayerCore_AudioEnded();
void ChunkerPlayerCore_Stop();
void ChunkerPlayerCore_Pause();
void ChunkerPlayerCore_Play();
int ChunkerPlayerCore_IsRunning();
void ChunkerPlayerCore_ResetAVQueues();
int ChunkerPlayerCore_EnqueueBlocks(const uint8_t *block, const int block_size);
void ChunkerPlayerCore_SetupOverlay(int width, int height);

#endif // _CHUNKER_PLAYER_CORE_H
