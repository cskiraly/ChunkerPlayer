/*
 *  Copyright (c) 2009-2011 Carmelo Daniele, Dario Marchese, Diego Reforgiato, Giuseppe Tropea
 *  developed for the Napa-Wine EU project. See www.napa-wine.eu
 *
 *  This is free software; see lgpl-2.1.txt
 */

#include "player_defines.h"
#include "player_stats.h"
#include "player_core.h"
#include "chunker_player.h"
#include <time.h>
#include <assert.h>

void ChunkerPlayerStats_Init()
{
	LastIFrameNumber = -1;
	LastQualityEstimation = 0.5f;
	qoe_adjust_factor = sqrt(VideoCallbackThreadParams.height*VideoCallbackThreadParams.width);
	
	if(LogTraces)
	{
		// rename all log files as well as the yuv file (new experiment)
		char tmp[255];
		LastLoggedVFrameNumber = 0;
		FirstLoggedVFrameNumber = -1;
		sprintf(tmp, "traces/%d_%s", ++ExperimentsCount, Channels[SelectedChannel].Title);
		CREATE_DIR(tmp);
		sprintf(VideoTraceFilename, "traces/%d_%s/videotrace.log", ExperimentsCount, Channels[SelectedChannel].Title);
		sprintf(AudioTraceFilename, "traces/%d_%s/audiotrace.log", ExperimentsCount, Channels[SelectedChannel].Title);
		sprintf(QoETraceFileName, "traces/%d_%s/qoe.log", ExperimentsCount, Channels[SelectedChannel].Title);
		
		// copy the loss pattern file
		FILE* fsrc = fopen("_chunklossrate.conf", "rb");
		if(fsrc)
		{
			sprintf(tmp, "traces/%d_%s/_chunklossrate.conf", ExperimentsCount, Channels[SelectedChannel].Title);
			FILE* fdst = fopen(tmp, "wb");
			if(fdst)
			{
				fseek(fsrc, 0, SEEK_END);
				int dst_size = ftell(fsrc);
				fseek(fsrc, 0, SEEK_SET);

				char* fbuffer = (char*) malloc(dst_size);
				fread((void*)fbuffer, dst_size, 1, fsrc);
				fwrite((void*) fbuffer, dst_size, 1, fdst);
				fclose(fdst);
				free(fbuffer);
			}
			fclose(fsrc);
		}
	}
	
#ifdef SAVE_YUV
	sprintf(YUVFileName, "traces/%d_%s/out_orig.yuv", ExperimentsCount, Channels[SelectedChannel].Title);
#endif
}

void ChunkerPlayerStats_UpdateAudioLossHistory(SHistory* history, long int frame_id, long int last_frame_extracted)
{
	// update packet history
	struct timeval now_tv;
	gettimeofday(&now_tv, NULL);
	if(last_frame_extracted > 0 && frame_id > last_frame_extracted)
	{
		int j, lost_frames = frame_id - last_frame_extracted - 1;
	
		SDL_LockMutex(history->Mutex);
		for(j=1; j<=lost_frames; j++)
		{
			history->History[history->Index].ID = last_frame_extracted+j;
			history->History[history->Index].Status = LOST_FRAME;
			history->History[history->Index].Time = now_tv;
			history->History[history->Index].Type = 5; // AUDIO
			history->Index = (history->Index+1)%QUEUE_HISTORY_SIZE;
#ifdef DEBUG_STATS
			if(LogTraces)
				assert(history->Index != history->LogIndex);
#else
		if(history->Index == history->LogIndex)
		{
			// unexpected full loss history buffer, must refresh trace files before continue...
			ChunkerPlayerStats_PrintHistoryTrace(history, AudioTraceFilename);
		}
#endif
			history->LostCount++;
		}
		SDL_UnlockMutex(history->Mutex);
	}
}

void ChunkerPlayerStats_UpdateVideoLossHistory(SHistory* history, long int frame_id, long int last_frame_extracted)
{
	// update packet history
	struct timeval now_tv;
	gettimeofday(&now_tv, NULL);
	if(last_frame_extracted > 0 && frame_id > last_frame_extracted)
	{
		int j, lost_frames = frame_id - last_frame_extracted - 1;
	
		SDL_LockMutex(history->Mutex);
		for(j=1; j<=lost_frames; j++)
		{
			history->History[history->Index].ID = last_frame_extracted+j;
			history->History[history->Index].Status = LOST_FRAME;
			history->History[history->Index].Time = now_tv;
			history->History[history->Index].Type = 0; // UNKNOWN VIDEO FRAME
			history->History[history->Index].Statistics.LastIFrameDistance = -1; // UNKNOWN
			history->Index = (history->Index+1)%QUEUE_HISTORY_SIZE;
#ifdef DEBUG_STATS
			if(LogTraces)
				assert(history->Index != history->LogIndex);
#else
			if(history->Index == history->LogIndex)
			{
				// unexpected full packet history buffer, must refresh trace files before continue...
				ChunkerPlayerStats_PrintHistoryTrace(history, VideoTraceFilename);
			}
#endif
			history->LostCount++;
		}
		SDL_UnlockMutex(history->Mutex);
	}
}

void ChunkerPlayerStats_UpdateAudioSkipHistory(SHistory* history, long int frame_id, int size)
{
	// update packet history
	struct timeval now_tv;
	gettimeofday(&now_tv, NULL);
	
	SDL_LockMutex(history->Mutex);
	history->History[history->Index].ID = frame_id;
	history->History[history->Index].Status = SKIPPED_FRAME;
	history->History[history->Index].Time = now_tv;
	history->History[history->Index].Size = size;
	history->History[history->Index].Type = 5; // AUDIO
	
	ChunkerPlayerStats_GetStats(history, &(history->History[history->Index].Statistics));
	
	history->Index=(history->Index+1)%QUEUE_HISTORY_SIZE;

#ifdef DEBUG_STATS
	if(LogTraces)
		assert(history->Index != history->LogIndex);
#else
	if(history->Index == history->LogIndex)
	{
		// unexpected full loss history buffer, must refresh trace files before continue...
		ChunkerPlayerStats_PrintHistoryTrace(history, AudioTraceFilename);
	}
#endif
	history->SkipCount++;
	SDL_UnlockMutex(history->Mutex);
}

void ChunkerPlayerStats_UpdateVideoSkipHistory(SHistory* history, long int frame_id, short int Type, int Size, AVFrame* pFrame)
{
	// update packet history
	struct timeval now_tv;
	gettimeofday(&now_tv, NULL);
	
	SDL_LockMutex(history->Mutex);
	history->History[history->Index].ID = frame_id;
	history->History[history->Index].Status = SKIPPED_FRAME;
	history->History[history->Index].Time = now_tv;
	history->History[history->Index].Size = Size;
	history->History[history->Index].Type = Type;
	ChunkerPlayerStats_GetStats(history, &(history->History[history->Index].Statistics));
	
	if(history->History[history->Index].Type == 1)
	{
		history->History[history->Index].Statistics.LastIFrameDistance = 0;
		LastIFrameNumber = frame_id;
	}
	else if(LastIFrameNumber > 0)
		history->History[history->Index].Statistics.LastIFrameDistance = frame_id-LastIFrameNumber;

	if(history->History[history->Index].Statistics.LastIFrameDistance >= 0 && (history->History[history->Index].Statistics.LastIFrameDistance < LastSourceIFrameDistance))
		LastSourceIFrameDistance = (unsigned char)history->History[history->Index].Statistics.LastIFrameDistance;
	
	history->Index = (history->Index+1)%QUEUE_HISTORY_SIZE;

#ifdef DEBUG_STATS
	if(LogTraces)
		assert(history->Index != history->LogIndex);
#else
	if(history->Index == history->LogIndex)
	{
		// unexpected full loss history buffer, must refresh trace files before continue...
		ChunkerPlayerStats_PrintHistoryTrace(history, VideoTraceFilename);
	}
#endif
	history->SkipCount++;
	SDL_UnlockMutex(history->Mutex);
}

void ChunkerPlayerStats_UpdateAudioPlayedHistory(SHistory* history, long int frame_id, int size)
{
	// update packet history
	struct timeval now_tv;
	gettimeofday(&now_tv, NULL);
	
	SDL_LockMutex(history->Mutex);
	history->History[history->Index].ID = frame_id;
	history->History[history->Index].Status = PLAYED_FRAME;
	history->History[history->Index].Time = now_tv;
	history->History[history->Index].Size = size;
	history->History[history->Index].Type = 5;
	ChunkerPlayerStats_GetStats(history, &(history->History[history->Index].Statistics));
	history->Index=(history->Index+1)%QUEUE_HISTORY_SIZE;

#ifdef DEBUG_STATS
	if(LogTraces)
		assert(history->Index != history->LogIndex);
#else
	if(history->Index == history->LogIndex)
	{
		// unexpected full loss history buffer, must refresh trace files before continue...
		ChunkerPlayerStats_PrintHistoryTrace(history, AudioTraceFilename);
	}
#endif

	history->PlayedCount++;
	SDL_UnlockMutex(history->Mutex);
}

void ChunkerPlayerStats_UpdateVideoPlayedHistory(SHistory* history, long int frame_id, short int Type, int Size, AVFrame* pFrame)
{
	// update packet history
	struct timeval now_tv;
	gettimeofday(&now_tv, NULL);
	
	SDL_LockMutex(history->Mutex);
	history->History[history->Index].ID = frame_id;
	history->History[history->Index].Status = PLAYED_FRAME;
	history->History[history->Index].Time = now_tv;
	history->History[history->Index].Size = Size;
	history->History[history->Index].Type = Type;
	ChunkerPlayerStats_GetStats(history, &(history->History[history->Index].Statistics));
	if(history->History[history->Index].Type == 1)
	{
		history->History[history->Index].Statistics.LastIFrameDistance = 0;
		LastIFrameNumber = frame_id;
	}
	else if(LastIFrameNumber > 0)
		history->History[history->Index].Statistics.LastIFrameDistance = frame_id-LastIFrameNumber;
	
	if(history->History[history->Index].Statistics.LastIFrameDistance >= 0 && (history->History[history->Index].Statistics.LastIFrameDistance < LastSourceIFrameDistance))
		LastSourceIFrameDistance = (unsigned char)history->History[history->Index].Statistics.LastIFrameDistance;

	history->Index=(history->Index+1)%QUEUE_HISTORY_SIZE;
#ifdef DEBUG_STATS
	if(LogTraces)
		assert(history->Index != history->LogIndex);
#else
	if(history->Index == history->LogIndex)
	{
		// unexpected full packet history buffer, must refresh trace files before continue...
		ChunkerPlayerStats_PrintHistoryTrace(history, VideoTraceFilename);
	}
#endif
	history->PlayedCount++;
	SDL_UnlockMutex(history->Mutex);
}

int ChunkerPlayerStats_GetMeanVideoQuality(SHistory* history, double* quality)
{
	static double qoe_reference_coeff = sqrt(QOE_REFERENCE_FRAME_WIDTH*QOE_REFERENCE_FRAME_HEIGHT);
	
	int counter = 0;
	SDL_LockMutex(history->Mutex);
		
	if(history->QoEIndex != history->Index)
	{
		int index;
		int end_index;
		int start_index;
		int tmp_index;
		double NN_inputs[7];
		int losses = 0;

		start_index = history->QoEIndex;
		end_index = history->Index-1;
		if(end_index < 0)
			end_index = QUEUE_HISTORY_SIZE-1;
		else if(history->Index < start_index)
			end_index += QUEUE_HISTORY_SIZE;
			
#ifdef DEBUG_STATS
		printf("DEBUG_STATS: start_index=%d, end_index=%d\n", start_index, end_index);
#endif
		
		int inside_burst = 0;
		int burst_size = 0;
		int burst_count = 0;
		double mean_burstiness = 0;
		for(index=start_index; (index<=end_index); index++)
		{
			tmp_index = index%QUEUE_HISTORY_SIZE;
#ifdef DEBUG_STATS
			if(LogTraces)
				assert(history->History[tmp_index].Type != 5);
#endif

			if((FirstLoggedVFrameNumber < 0))
				FirstLoggedVFrameNumber = history->History[tmp_index].ID;
			LastLoggedVFrameNumber = history->History[tmp_index].ID;
			VideoFramesLogged[history->History[tmp_index].Status]++;

			if(history->History[tmp_index].Status == LOST_FRAME)
			{
				losses++;
				inside_burst = 1;
				burst_size++;
			}
			else
			{
				if(inside_burst)
				{
					inside_burst = 0;
					mean_burstiness += burst_size;
					burst_size = 0;
					burst_count++;
				}
			}
			
			counter++;
		}
		if(inside_burst)
		{
			inside_burst = 0;
			mean_burstiness += burst_size;
			burst_size = 0;
			burst_count++;
		}
		
		if(burst_count > 0)
			mean_burstiness /= ((double)burst_count);

		// adjust bitrate with respect to the qoe reference resolution/bitrate ratio
		NN_inputs[0] = ((double)Channels[SelectedChannel].Bitrate) * (qoe_reference_coeff/qoe_adjust_factor) / 1000;
		NN_inputs[1] = ((double)losses)/((double)counter) * 100;
		NN_inputs[2] = mean_burstiness;
		
#ifdef DEBUG_STATS
		printf("NN_inputs[0] = %.3f, NN_inputs[1] = %.3f, NN_inputs[2] = %.3f\n", NN_inputs[0], NN_inputs[1], NN_inputs[2]);
#endif
		QoE_Estimator(NN_inputs, quality);
		
		if(LogTraces)
		{
			FILE* tracefile = NULL;
			tracefile = fopen(QoETraceFileName, "a");
			if(tracefile)
			{
				// bitrate (Kbits/sec) loss_percentage loss_burstiness est_mean_psnr
				fprintf(tracefile, "%d %.3f %.3f %.3f\n", (int)((Channels[SelectedChannel].Bitrate) * (qoe_reference_coeff/qoe_adjust_factor) / 1000), (float)(((double)losses)/((double)counter) * 100), (float)mean_burstiness, (float)(*quality));
				fclose(tracefile);
			}
		}
		
		history->QoEIndex = (end_index+1)%QUEUE_HISTORY_SIZE;
	}
	SDL_UnlockMutex(history->Mutex);
	
	return counter;
}
/**
 * returns 1 if statistics data changed
 */
int ChunkerPlayerStats_GetStats(SHistory* history, SStats* statistics)
{
	struct timeval now;
	int lost=0, played=0, skipped=0, index, i;
	int bytes = 0;
	
	gettimeofday(&now, NULL);
	
	SDL_LockMutex(history->Mutex);

	index = history->Index-1;
	for(i=1; i<QUEUE_HISTORY_SIZE; i++)
	{
		if(index<0)
			index = QUEUE_HISTORY_SIZE-1;

		if((((history->History[index].Time.tv_sec*1000)+(history->History[index].Time.tv_usec/1000)) > ((now.tv_sec*1000)+(now.tv_usec/1000) - MAIN_STATS_WINDOW)))
		{
			switch(history->History[index].Status)
			{
				case LOST_FRAME:
					lost++;
					break;
				case PLAYED_FRAME:
					played++;
					bytes+=history->History[index].Size;
					break;
				case SKIPPED_FRAME:
					skipped++;
					bytes+=history->History[index].Size;
					break;
			}
			index--;
			continue;
		}
		break;
	}

	statistics->Lossrate = (int)(((double)lost)/((double)(MAIN_STATS_WINDOW/1000)));
	statistics->Skiprate = (int)(((double)skipped)/((double)(MAIN_STATS_WINDOW/1000)));
	statistics->Bitrate = (int)((((double)bytes)/1000*8)/((double)(MAIN_STATS_WINDOW/1000)));
	
	double tot = (double)(skipped+played+lost);
	if(tot > 0)
	{
		statistics->PercLossrate = (int)(((double)lost)/tot* 100);
		statistics->PercSkiprate = (int)(((double)skipped)/tot* 100);
	}
	else
		statistics->PercLossrate = statistics->PercSkiprate = 0;
	
	SDL_UnlockMutex(history->Mutex);
	
	return (lost || played || skipped || bytes);
}

void ChunkerPlayerStats_PrintContextFile()
{
	// edit yuv log file
	char tmp[255];
	sprintf(tmp, "traces/%d_%s/player_context.txt", ExperimentsCount, Channels[SelectedChannel].Title);
	FILE* tmp_file = fopen(tmp, "w");
	if(tmp_file)
	{
		fprintf(tmp_file, "width = %d\n", VideoCallbackThreadParams.width);
		fprintf(tmp_file, "height = %d\n", VideoCallbackThreadParams.height);
		fprintf(tmp_file, "total_video_frames_logged = %ld\n", VideoFramesLogged[0]+VideoFramesLogged[1]+VideoFramesLogged[2]);
		fprintf(tmp_file, "first_video_frame_number = %ld\n", FirstLoggedVFrameNumber);
		fprintf(tmp_file, "last_video_frame_number = %ld\n", LastLoggedVFrameNumber);
		fprintf(tmp_file, "total_video_frames_decoded = %ld\n", VideoFramesLogged[PLAYED_FRAME]);
		fprintf(tmp_file, "skipped_video_frames = %ld\n", VideoFramesLogged[SKIPPED_FRAME]);
		fprintf(tmp_file, "lost_video_frames = %ld\n", VideoFramesLogged[LOST_FRAME]);
		fclose(tmp_file);
	}
}

int ChunkerPlayerStats_PrintHistoryTrace(SHistory* history, char* tracefilename)
{
	int counter = 0;
	SDL_LockMutex(history->Mutex);

#ifdef DEBUG_STATS	
	if(LogTraces)
		assert(tracefilename != NULL);
#endif
	FILE* tracefile = NULL;
	tracefile = fopen(tracefilename, "a");
	if(tracefile)
	{
	    if(history->LogIndex != history->Index)
	    {
		    int index;
		    int end_index;
		    int start_index;

		    start_index = history->LogIndex;
		    end_index = history->Index-1;
		    if(end_index < 0)
			    end_index = QUEUE_HISTORY_SIZE-1;
		    else if(history->Index < start_index)
			    end_index += QUEUE_HISTORY_SIZE;
		
		    for(index=start_index; (index<=end_index); index++)
		    {
			    int id = history->History[index%QUEUE_HISTORY_SIZE].ID;
			    int status = history->History[index%QUEUE_HISTORY_SIZE].Status;
			    int lossrate = -1, skiprate = -1, perc_lossrate = -1, perc_skiprate = -1, lastiframe_dist = -1;
			    int bitrate = -1;
			    char type = '?';
			    switch(history->History[index%QUEUE_HISTORY_SIZE].Type)
			    {
				    case 1:
					    type = 'I';
					    break;
				    case 2:
					    type = 'P';
					    break;
				    case 3:
					    type = 'B';
					    break;
				    case 5:
					    type = 'A';
					    break;
			    }
			    if(history->History[index%QUEUE_HISTORY_SIZE].Type != 5)
			    {
				    if((FirstLoggedVFrameNumber < 0))
					    FirstLoggedVFrameNumber = id;
				    LastLoggedVFrameNumber = id;
				    VideoFramesLogged[status]++;
			    }
			    if(status != LOST_FRAME)
			    {
				    lossrate = history->History[index%QUEUE_HISTORY_SIZE].Statistics.Lossrate;
				    skiprate = history->History[index%QUEUE_HISTORY_SIZE].Statistics.Skiprate;
				    perc_lossrate = history->History[index%QUEUE_HISTORY_SIZE].Statistics.PercLossrate;
				    perc_skiprate = history->History[index%QUEUE_HISTORY_SIZE].Statistics.PercSkiprate;
				    if(type != 'A')
				    {
					    lastiframe_dist = history->History[index%QUEUE_HISTORY_SIZE].Statistics.LastIFrameDistance;
				    }
				    bitrate = history->History[index%QUEUE_HISTORY_SIZE].Statistics.Bitrate;
			    }
			    fprintf(tracefile, "%d %d %c %d %d %d %d %d %d\n",
				    id, status, type, lossrate, skiprate, perc_lossrate, perc_skiprate, lastiframe_dist, bitrate);
			    counter++;
		    }
		    history->LogIndex = (end_index+1)%QUEUE_HISTORY_SIZE;
	    }
	    fclose(tracefile);
	}
		
	SDL_UnlockMutex(history->Mutex);
	return counter;
}
