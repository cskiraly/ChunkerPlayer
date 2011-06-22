#ifndef _CHUNKER_PLAYER_STATS_H
#define _CHUNKER_PLAYER_STATS_H

#include "player_core.h"

#define MAX_BITRATE 1000
#define MIN_BITRATE 0

#define LOST_FRAME 		0
#define PLAYED_FRAME 	1
#define SKIPPED_FRAME	2

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

void ChunkerPlayerStats_Init();

void ChunkerPlayerStats_UpdateAudioLossHistory(SHistory* history, long int frame_id, long int last_frame_extracted);
void ChunkerPlayerStats_UpdateVideoLossHistory(SHistory* history, long int frame_id, long int last_frame_extracted);

void ChunkerPlayerStats_UpdateAudioSkipHistory(SHistory* history, long int frame_id, int size);
void ChunkerPlayerStats_UpdateVideoSkipHistory(SHistory* history, long int frame_id, short int Type, int Size, AVFrame* frame);

void ChunkerPlayerStats_UpdateAudioPlayedHistory(SHistory* history, long int frame_id, int size);
void ChunkerPlayerStats_UpdateVideoPlayedHistory(SHistory* history, long int frame_id, short int Type, int Size, AVFrame* frame);

int ChunkerPlayerStats_GetMeanVideoQuality(SHistory* history, int real_bitrate ,double* quality);

int ChunkerPlayerStats_GetStats(SHistory* history, SStats* statistics);

#endif
