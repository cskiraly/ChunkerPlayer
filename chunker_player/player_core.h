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

#define QUEUE_HISTORY_SIZE (STATISTICS_WINDOW_SIZE*MAX_FPS)

typedef struct threadVal {
	int width;
	int height;
} ThreadVal;

typedef struct SStats
{
	int Lossrate;
	int Skiprate;
	int PercLossrate;
	int PercSkiprate;
	int Bitrate; // Kbits/sec
	int LastIFrameDistance; // distance from the last received intra-frame
} SStats;

typedef struct SHistoryElement
{
	long int ID;
	struct timeval Time;
	short int Type;
	int Size; // size in bytes
	unsigned char Status; // 0 lost; 1 played; 2 skipped
	SStats Statistics;
} SHistoryElement;

typedef struct SHistory
{
	SHistoryElement History[QUEUE_HISTORY_SIZE];
	int Index; // the position where the next history element will be inserted in
	int LogIndex; // the position of the next element that will be logged to file
	int QoEIndex; // the position of the next element that will used to evaluate QoE
	long int LostCount;
	long int PlayedCount;
	long int SkipCount;
	SDL_mutex *Mutex;
} SHistory;

typedef struct PacketQueue {
	AVPacketList *first_pkt;
	AVPacket *minpts_pkt;
	AVPacketList *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	short int queueType;
	int last_frame_extracted; //HINT THIS SHOULD BE MORE THAN 4 BYTES
	//total frames lost, as seen from the queue, since last queue init
	int total_lost_frames;
	
	SHistory PacketHistory;
	
	double density;
	char stats_message[255];
} PacketQueue;

AVCodecContext  *aCodecCtx;
SDL_Thread *video_thread;
SDL_Thread *stats_thread;
uint8_t *outbuf_audio;
// short int QueueFillingMode=1;
short int QueueStopped;
ThreadVal VideoCallbackThreadParams;

int AudioQueueOffset;
PacketQueue audioq;
PacketQueue videoq;
AVPacket AudioPkt, VideoPkt;
int AVPlaying;
int CurrentAudioFreq;
int CurrentAudioSamples;

SDL_Rect *InitRect;
SDL_AudioSpec *AudioSpecification;

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

char VideoTraceFilename[1024];
char AudioTraceFilename[1024];
char QoETraceFileName[1024];

char VideoFrameLossRateLogFilename[256];
char VideoFrameSkipRateLogFilename[256];

long int decoded_vframes;
long int LastSavedVFrame;
unsigned char LastSourceIFrameDistance;

int ChunkerPlayerCore_InitCodecs(char *v_codec, int width, int height, char *audio_codec, int sample_rate, short int audio_channels);
int ChunkerPlayerCore_AudioEnded();
void ChunkerPlayerCore_Stop();
void ChunkerPlayerCore_Play();
int ChunkerPlayerCore_IsRunning();
void ChunkerPlayerCore_ResetAVQueues();
int ChunkerPlayerCore_PacketQueuePut(PacketQueue *q, AVPacket *pkt);
int ChunkerPlayerCore_EnqueueBlocks(const uint8_t *block, const int block_size);
void ChunkerPlayerCore_SetupOverlay(int width, int height);

#endif // _CHUNKER_PLAYER_CORE_H
