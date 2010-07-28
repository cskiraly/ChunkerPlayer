#ifndef _CHUNKER_PLAYER_CORE_H
#define _CHUNKER_PLAYER_CORE_H

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <microhttpd.h>
#include "external_chunk_transcoding.h"
#include "frame.h"
#include "player_defines.h"
#include <SDL.h>
#include <SDL_thread.h>
#include <SDL_mutex.h>
// #include <SDL_ttf.h>
// #include <SDL_image.h>
#include <SDL_video.h>

typedef struct threadVal {
	int width;
	int height;
	// float aspect_ratio;
} ThreadVal;

typedef struct PacketQueue {
	AVPacketList *first_pkt;
	AVPacketList *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	short int queueType;
	int last_frame_extracted; //HINT THIS SHOULD BE MORE THAN 4 BYTES
	int total_lost_frames;
	int loss_history[LOSS_HISTORY_MAX_SIZE];
	int instant_lost_frames;
	int history_index;
	double density;
} PacketQueue;

AVCodecContext  *aCodecCtx;
SDL_Thread *video_thread;
uint8_t *outbuf_audio;
// short int QueueFillingMode=1;
short int QueueStopped;
ThreadVal VideoCallbackThreadParams;

int AudioQueueOffset;
PacketQueue audioq;
PacketQueue videoq;
AVPacket AudioPkt, VideoPkt;
// int quit = 0;
// int SaveYUV=0;
int AVPlaying;
// char YUVFileName[256];

// int queue_filling_threshold = 0;

SDL_Rect *InitRect;
SDL_AudioSpec AudioSpecification;

struct SwsContext *img_convert_ctx;
int GotSigInt;

long long DeltaTime;
short int FirstTimeAudio, FirstTime;

int dimAudioQ;
float deltaAudioQ;
float deltaAudioQError;

void SaveFrame(AVFrame *pFrame, int width, int height);
// int P2PProcessID = -1;

int ChunkerPlayerCore_InitCodecs(int width, int height, int sample_rate, short audio_channels);
int ChunkerPlayerCore_VideoEnded();
void ChunkerPlayerCore_Stop();
void ChunkerPlayerCore_Play();
int ChunkerPlayerCore_IsRunning();
void ChunkerPlayerCore_ResetAVQueues();
int ChunkerPlayerCore_PacketQueuePut(PacketQueue *q, AVPacket *pkt);
int ChunkerPlayerCore_EnqueueBlocks(const uint8_t *block, const int block_size);
void ChunkerPlayerCore_SetupOverlay(int width, int height);

int VideoCallback(void *valthread);
void AudioCallback(void *userdata, Uint8 *stream, int len);

#endif // _CHUNKER_PLAYER_CORE_H
