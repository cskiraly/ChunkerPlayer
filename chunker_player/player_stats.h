#ifndef _CHUNKER_PLAYER_STATS_H
#define _CHUNKER_PLAYER_STATS_H

#include <libavcodec/avcodec.h>
#include <SDL_mutex.h>

#include "player_defines.h"
#include "player_core.h"

#define MAX_BITRATE 1000
#define MIN_BITRATE 0

#define LOST_FRAME 		0
#define PLAYED_FRAME 	1
#define SKIPPED_FRAME	2

#define QUEUE_HISTORY_SIZE (STATISTICS_WINDOW_SIZE*MAX_FPS)

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

char VideoTraceFilename[1024];
char AudioTraceFilename[1024];
char QoETraceFileName[1024];

int LastIFrameNumber;
double LastQualityEstimation;
double qoe_adjust_factor;

long int FirstLoggedVFrameNumber;
long int LastLoggedVFrameNumber;
int ExperimentsCount;
// lost, played and skipped video frames
long int VideoFramesLogged[3];
// lost, played and skipped audio frames
long int AudioFramesLogged[3];
void ChunkerPlayerStats_PrintContextFile();

void ChunkerPlayerStats_Init(ThreadVal *VideoCallbackThreadParams);

void ChunkerPlayerStats_UpdateAudioLossHistory(SHistory* history, long int frame_id, long int last_frame_extracted);
void ChunkerPlayerStats_UpdateVideoLossHistory(SHistory* history, long int frame_id, long int last_frame_extracted);

void ChunkerPlayerStats_UpdateAudioSkipHistory(SHistory* history, long int frame_id, int size);
void ChunkerPlayerStats_UpdateVideoSkipHistory(SHistory* history, long int frame_id, short int Type, int Size, AVFrame* frame);

void ChunkerPlayerStats_UpdateAudioPlayedHistory(SHistory* history, long int frame_id, int size);
void ChunkerPlayerStats_UpdateVideoPlayedHistory(SHistory* history, long int frame_id, short int Type, int Size, AVFrame* frame);

int ChunkerPlayerStats_PrintHistoryTrace(SHistory* history, char* tracefilename);

int ChunkerPlayerStats_GetMeanVideoQuality(SHistory* history, int real_bitrate ,double* quality);

int ChunkerPlayerStats_GetStats(SHistory* history, SStats* statistics);

#endif
