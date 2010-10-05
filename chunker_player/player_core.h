#ifndef _CHUNKER_PLAYER_CORE_H
#define _CHUNKER_PLAYER_CORE_H

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <stdio.h>
#include <unistd.h>
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
} ThreadVal;

typedef struct PacketQueue {
	AVPacketList *first_pkt;
	AVPacketList *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	short int queueType;
	int last_frame_extracted; //HINT THIS SHOULD BE MORE THAN 4 BYTES
	//total frames lost, as seen from the queue, since last queue init
	int total_lost_frames;
	int loss_history[LOSS_HISTORY_MAX_SIZE];
	int loss_history_index;
	//how many frames we are loosing at the moment, calculated over a short sliding time window
	//i.e. half a second, expressed in lost_frames/sec
	double instant_lost_frames;
	double density;
	//total number of skip events, as seen from the queue, since last queue init
	int total_skips;
	int last_skips; //the valued before updating it, for computing delta
	int skip_history[LOSS_HISTORY_MAX_SIZE];
	int skip_history_index;
	//how many skips we are observing, calculated over a short sliding time window
	//i.e. half a second, expressed in skips/sec
	double instant_skips;
	int instant_window_size; //averaging window size, self-correcting based on window_seconds
	int instant_window_size_target;
	int instant_window_seconds; //we want to compute number of events in a 1sec wide window
	int last_window_size_update;
	char stats_message[255];
	int last_stats_display;
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
int AVPlaying;

SDL_Rect *InitRect;
SDL_AudioSpec AudioSpecification;

struct SwsContext *img_convert_ctx;
int GotSigInt;

long long DeltaTime;
short int FirstTimeAudio, FirstTime;

int dimAudioQ;
float deltaAudioQ;
float deltaAudioQError;

int SaveYUV;
char YUVFileName[256];
int SaveLoss;
char LossTracesFilename[256];

int ChunkerPlayerCore_InitCodecs(int width, int height, int sample_rate, short audio_channels);
int ChunkerPlayerCore_AudioEnded();
void ChunkerPlayerCore_Stop();
void ChunkerPlayerCore_Play();
int ChunkerPlayerCore_IsRunning();
void ChunkerPlayerCore_ResetAVQueues();
int ChunkerPlayerCore_PacketQueuePut(PacketQueue *q, AVPacket *pkt);
int ChunkerPlayerCore_EnqueueBlocks(const uint8_t *block, const int block_size);
void ChunkerPlayerCore_SetupOverlay(int width, int height);

#endif // _CHUNKER_PLAYER_CORE_H
