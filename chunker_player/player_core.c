/*
 *  Copyright (c) 2009-2011 Carmelo Daniele, Dario Marchese, Diego Reforgiato, Giuseppe Tropea
 *  developed for the Napa-Wine EU project. See www.napa-wine.eu
 *
 *  This is free software; see lgpl-2.1.txt
 */

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <stdio.h>
#include <unistd.h>
#include <microhttpd.h>
#include "external_chunk_transcoding.h"
#include "frame.h"
#include <SDL.h>
#include <SDL_thread.h>
#include <SDL_mutex.h>
// #include <SDL_ttf.h>
// #include <SDL_image.h>
#include <SDL_video.h>
#include <assert.h>
#include <time.h>

#include "player_stats.h"
#include "player_defines.h"
#include "chunker_player.h"
#include "player_gui.h"
#include "player_core.h"
#include "player_stats.h"

SDL_Overlay *YUVOverlay;

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
	long cumulative_bitrate;
	long cumulative_samples;

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
uint8_t CurrentAudioSilence;

int GotSigInt;

long long DeltaTime;
short int FirstTimeAudio, FirstTime;

int dimAudioQ;
float deltaAudioQ;
float deltaAudioQError;

int SaveYUV;
char YUVFileName[256];
int SaveLoss;

char VideoFrameLossRateLogFilename[256];
char VideoFrameSkipRateLogFilename[256];

long int decoded_vframes;
long int LastSavedVFrame;

void SaveFrame(AVFrame *pFrame, int width, int height);
int VideoCallback(void *valthread);
int CollectStatisticsThread(void *params);
void AudioCallback(void *userdata, Uint8 *stream, int len);
void PacketQueueClearStats(PacketQueue *q);

//int lastCheckedVideoFrame = -1;
long int last_video_frame_extracted = -1;

int timeval_subtract(struct timeval* x, struct timeval* y, struct timeval* result)
{
  // Perform the carry for the later subtraction by updating y.
  if (x->tv_usec < y->tv_usec)
  {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000)
  {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  // Compute the time remaining to wait. tv_usec is certainly positive.
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  // Return 1 if result is negative.
  return x->tv_sec < y->tv_sec;
}


void PacketQueueInit(PacketQueue *q, short int Type)
{
#ifdef DEBUG_QUEUE
	printf("QUEUE: INIT BEGIN: NPackets=%d Type=%s\n", q->nb_packets, (q->queueType==AUDIO) ? "AUDIO" : "VIDEO");
#endif
	memset(q,0,sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	QueueFillingMode=1;
	q->queueType=Type;
	q->last_frame_extracted = -1;
	q->first_pkt= NULL;
	q->minpts_pkt= NULL;
	//q->last_pkt = NULL;
	q->nb_packets = 0;
	q->size = 0;
	q->density= 0.0;
	FirstTime = 1;
	FirstTimeAudio = 1;
	//init up statistics
	
	q->PacketHistory.Mutex = SDL_CreateMutex();
	PacketQueueClearStats(q);
	
#ifdef DEBUG_QUEUE
	printf("QUEUE: INIT END: NPackets=%d Type=%s\n", q->nb_packets, (q->queueType==AUDIO) ? "AUDIO" : "VIDEO");
#endif
}

void PacketQueueReset(PacketQueue *q)
{
	AVPacketList *tmp,*tmp1;
#ifdef DEBUG_QUEUE
	printf("QUEUE: RESET BEGIN: NPackets=%d Type=%s LastExtr=%d\n", q->nb_packets, (q->queueType==AUDIO) ? "AUDIO" : "VIDEO", q->last_frame_extracted);
#endif
	SDL_LockMutex(q->mutex);

	tmp = q->first_pkt;
	while(tmp) {
		tmp1 = tmp;
		tmp = tmp->next;
		av_free_packet(&(tmp1->pkt));
		av_free(tmp1);
#ifdef DEBUG_QUEUE
		printf("F ");
#endif
		q->PacketHistory.LostCount++;
	}
#ifdef DEBUG_QUEUE
	printf("\n");
#endif

	QueueFillingMode=1;
	q->last_frame_extracted = -1;
	
	// on queue reset do not reset loss count
	// (loss count reset is done on queue init, ie channel switch)
	q->density=0.0;
	q->first_pkt= NULL;
	q->minpts_pkt= NULL;
	//q->last_pkt = NULL;
	q->nb_packets = 0;
	q->size = 0;
	FirstTime = 1;
	FirstTimeAudio = 1;
	//clean up statistics
	PacketQueueClearStats(q);
#ifdef DEBUG_QUEUE
	printf("QUEUE: RESET END: NPackets=%d Type=%s LastExtr=%d\n", q->nb_packets, (q->queueType==AUDIO) ? "AUDIO" : "VIDEO", q->last_frame_extracted);
#endif
	SDL_UnlockMutex(q->mutex);
}

void PacketQueueClearStats(PacketQueue *q)
{
	sprintf(q->stats_message, "%s", "\n");
	int i;
	memset((void*)q->PacketHistory.History, 0, sizeof(SHistoryElement)*QUEUE_HISTORY_SIZE);
	for(i=0; i<QUEUE_HISTORY_SIZE; i++)
	{
		q->PacketHistory.History[i].Statistics.LastIFrameDistance = -1;
		q->PacketHistory.History[i].Status = -1;
	}
	q->PacketHistory.Index = q->PacketHistory.LogIndex = 0;
	q->PacketHistory.Index = q->PacketHistory.QoEIndex = 0;
	q->PacketHistory.LostCount = q->PacketHistory.PlayedCount = q->PacketHistory.SkipCount = 0;
}

int ChunkerPlayerCore_PacketQueuePut(PacketQueue *q, AVPacket *pkt)
{
	//~ printf("\tSTREAM_INDEX=%d\n", pkt->stream_index);
	short int skip = 0;
	AVPacketList *pkt1, *tmp, *prevtmp;
	int res = 0;

	if(q->nb_packets > queue_filling_threshold*QUEUE_MAX_GROW_FACTOR) {
#ifdef DEBUG_QUEUE
		printf("QUEUE: PUT i have TOO MANY packets %d Type=%s, RESETTING\n", q->nb_packets, (q->queueType==AUDIO) ? "AUDIO" : "VIDEO");
#endif
		PacketQueueReset(q);
	}

	//make a copy of the incoming packet
	if(av_dup_packet(pkt) < 0) {
#ifdef DEBUG_QUEUE
		printf("QUEUE: PUT in Queue cannot duplicate in packet	: NPackets=%d Type=%s\n",q->nb_packets, (q->queueType==AUDIO) ? "AUDIO" : "VIDEO");
#endif
		return -1;
	}
	pkt1 = av_malloc(sizeof(AVPacketList));

	if(!pkt1) {
		av_free_packet(pkt);
		return -1;
	}
	pkt1->pkt = *pkt;
	pkt1->next = NULL;
	
	static time_t last_auto_switch = 0;

	if(
		(pkt->stream_index < last_video_frame_extracted)
		&& (pkt->stream_index <= RESTART_FRAME_NUMBER_THRESHOLD)
		&& ((time(NULL) - last_auto_switch) > 10)
	)
	{
		printf("file streaming loop detected => re-tune channel and start grabbing statistics\n");
		last_auto_switch = time(NULL);
		SDL_LockMutex(q->mutex);
		ReTune(&(Channels[SelectedChannel]));
		SDL_UnlockMutex(q->mutex);
	}

	else
	{
		SDL_LockMutex(q->mutex);

		// INSERTION SORT ALGORITHM
		// before inserting pkt, check if pkt.stream_index is <= current_extracted_frame.
		if(pkt->stream_index > q->last_frame_extracted)
		{
			// either checking starting from the first_pkt or needed other struct like AVPacketList with next and prev....
			//if (!q->last_pkt)
			if(!q->first_pkt) {
				q->first_pkt = pkt1;
				q->last_pkt = pkt1;
			}
			else if(pkt->stream_index < q->first_pkt->pkt.stream_index) {
				//the packet that has arrived is earlier than the first we got some time ago!
				//we need to put it at the head of the queue
				pkt1->next = q->first_pkt;
				q->first_pkt = pkt1;
			}
			else {
				tmp = q->first_pkt;
				while(tmp->pkt.stream_index < pkt->stream_index) {
					prevtmp = tmp;
					tmp = tmp->next;

					if(!tmp) {
						break;
					}
				}
				if(tmp && tmp->pkt.stream_index == pkt->stream_index) {
					//we already have a frame with that index
					skip = 1;
#ifdef DEBUG_QUEUE
					printf("%s QUEUE: PUT: we already have frame with index %d, skipping\n", ((q->queueType == AUDIO) ? "AUDIO" : "VIDEO"), pkt->stream_index);
#endif
				}
				else {
					prevtmp->next = pkt1;
					pkt1->next = tmp;
					if(pkt1->next == NULL)
						q->last_pkt = pkt1;
				}
				//q->last_pkt->next = pkt1; // It was uncommented when not insertion sort
			}
			if(skip == 0) {
				//q->last_pkt = pkt1;
				q->nb_packets++;
				q->size += pkt1->pkt.size;
				if(q->nb_packets>=queue_filling_threshold && QueueFillingMode) // && q->queueType==AUDIO)
				{
					QueueFillingMode=0;
#ifdef DEBUG_QUEUE
					printf("QUEUE: PUT: FillingMode set to zero\n");
#endif
				}
				//set min
				if (!q->minpts_pkt || (pkt1->pkt.pts < q->minpts_pkt->pts)) {
					q->minpts_pkt = &(pkt1->pkt);
				}
			}
		}
		else {
			av_free_packet(&pkt1->pkt);
			av_free(pkt1);
#ifdef DEBUG_QUEUE
			printf("QUEUE: PUT: NOT inserting because index %d <= last extracted %d\n", pkt->stream_index, q->last_frame_extracted);
#endif
			res = 1;
		}
		SDL_UnlockMutex(q->mutex);
	}

	return res;
}

int OpenACodec (char *audio_codec, int sample_rate, short int audio_channels)
{
	AVCodec *aCodec;

	aCodecCtx = avcodec_alloc_context();
	//aCodecCtx->bit_rate = 64000;
	aCodecCtx->sample_rate = sample_rate;
	aCodecCtx->channels = audio_channels;
	aCodec = avcodec_find_decoder_by_name(audio_codec);
	if(!aCodec) {
		printf("Codec not found!\n");
		return -1;
	}
	if(avcodec_open(aCodecCtx, aCodec)<0) {
		fprintf(stderr, "could not open codec\n");
		return -1; // Could not open codec
	}
	printf("using audio Codecid: %d ",aCodecCtx->codec_id);
	printf("samplerate: %d ",aCodecCtx->sample_rate);
	printf("channels: %d\n",aCodecCtx->channels);

	return 1;
}

int OpenAudio(AVCodecContext  *aCodecCtx)
{
	SDL_AudioSpec *wanted_spec;
	static SDL_AudioSpec *wanted_spec_old = NULL;

	if (! (wanted_spec = malloc(sizeof(*wanted_spec)))) {
		perror("error initializing audio");
		return -1;
	}
	wanted_spec->freq = aCodecCtx->sample_rate;
	wanted_spec->format = AUDIO_S16SYS;
	wanted_spec->channels = aCodecCtx->channels;
	wanted_spec->silence = 0;
	wanted_spec->samples = SDL_AUDIO_BUFFER_SIZE;
	wanted_spec->callback = AudioCallback;
	wanted_spec->userdata = aCodecCtx;

#ifdef DEBUG_AUDIO
	printf("wanted freq:%d\n",wanted_spec->freq);
	printf("wanted format:%d\n",wanted_spec->format);
	printf("wanted channels:%d\n",wanted_spec->channels);
	printf("wanted silence:%d\n",wanted_spec->silence);
	printf("wanted samples:%d\n",wanted_spec->samples);
#endif

	if (wanted_spec_old && 
	   (wanted_spec->freq == wanted_spec_old->freq) &&
	   (wanted_spec->channels == wanted_spec_old->channels)) {	//do not reinit audio if the wanted specification is the same as before
		return 1;
	}

	if(wanted_spec_old) {
		SDL_CloseAudio();
	}

	if (! (wanted_spec_old = malloc(sizeof(*wanted_spec_old)))) {
		perror("error initializing audio");
		return -1;
	}
	memcpy(wanted_spec_old, wanted_spec, sizeof(*wanted_spec));

	if (SDL_OpenAudio(wanted_spec,NULL)<0) {
		fprintf(stderr,"SDL_OpenAudio: %s\n", SDL_GetError());
		return -1;
	}

	CurrentAudioFreq = wanted_spec->freq;
	CurrentAudioSamples = wanted_spec->samples;
	dimAudioQ = wanted_spec->size;
	deltaAudioQ = (float)((float)wanted_spec->samples)*1000/wanted_spec->freq;	//in ms
	CurrentAudioSilence = wanted_spec->silence;

#ifdef DEBUG_AUDIO
	printf("freq:%d\n",wanted_spec->freq);
	printf("format:%d\n",wanted_spec->format);
	printf("channels:%d\n",wanted_spec->channels);
	printf("silence:%d\n",wanted_spec->silence);
	printf("samples:%d\n",wanted_spec->samples);
	printf("size:%d\n",wanted_spec->size);
	printf("deltaAudioQ: %f\n",deltaAudioQ);
#endif

	return 1;
}

int ChunkerPlayerCore_InitCodecs(char *v_codec, int width, int height, char *audio_codec, int sample_rate, short int audio_channels)
{
	// some initializations
	QueueStopped = 0;
	AudioQueueOffset=0;
	AVPlaying = 0;
	GotSigInt = 0;
	FirstTimeAudio=1;
	FirstTime = 1;
	deltaAudioQError=0;
	memset(&VideoCallbackThreadParams, 0, sizeof(ThreadVal));
	
	VideoCallbackThreadParams.width = width;
	VideoCallbackThreadParams.height = height;
	VideoCallbackThreadParams.video_codec = strdup(v_codec);

	// Register all formats and codecs
	avcodec_init();
	av_register_all();

	if (OpenACodec(audio_codec, sample_rate, audio_channels) < 0) {
		return -1;
	}

	if (OpenAudio(aCodecCtx) < 1) {
		return -1;
	}

	outbuf_audio = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);

	//initialize the audio and the video queues
	PacketQueueInit(&audioq, AUDIO);
	PacketQueueInit(&videoq, VIDEO);
	
	// Init audio and video buffers
	av_init_packet(&AudioPkt);
	av_init_packet(&VideoPkt);
	//printf("AVCODEC_MAX_AUDIO_FRAME_SIZE=%d\n", AVCODEC_MAX_AUDIO_FRAME_SIZE);
	AudioPkt.data=(uint8_t *)malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
	if(!AudioPkt.data) return 1;
	VideoPkt.data=(uint8_t *)malloc(width*height*3/2);
	if(!VideoPkt.data) return 1;

	char audio_stats[255], video_stats[255];
	sprintf(audio_stats, "waiting for incoming audio packets...");
	sprintf(video_stats, "waiting for incoming video packets...");
	ChunkerPlayerGUI_SetStatsText(audio_stats, video_stats,LED_GREEN);
	
	return 0;
}

int DecodeEnqueuedAudio(AVPacket *pkt, PacketQueue *q, int* size)
{
	uint16_t *audio_bufQ = NULL;
	int16_t *dataQ = NULL;
	int data_sizeQ = AVCODEC_MAX_AUDIO_FRAME_SIZE;
	int lenQ;
	int ret = 0;

	//set the flag to decoded anyway	
	pkt->convergence_duration = -1;

	audio_bufQ = (uint16_t *)av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
	if(audio_bufQ) {
#ifdef DEBUG_AUDIO_BUFFER
		printf("AUDIO_BUFFER: about to decode packet %d, size %d, data %d\n", pkt->stream_index, pkt->size, pkt->data);
#endif
		//decode the packet data
		lenQ = avcodec_decode_audio3(aCodecCtx, (int16_t *)audio_bufQ, &data_sizeQ, pkt);
		if(lenQ > 0) {
			dataQ = (int16_t *)av_malloc(data_sizeQ); //this will be free later at the time of playback
			if(dataQ) {
				memcpy(dataQ, audio_bufQ, data_sizeQ);
				if(pkt->data != NULL)
				{
					//discard the old encoded bytes
					av_free(pkt->data);
				}
				//subtract them from queue size
				q->size -= pkt->size;
				*size = pkt->size;
				pkt->data = (uint8_t *)dataQ;
				pkt->size = data_sizeQ;
				//add new size to queue size
				q->size += pkt->size;
				ret = 1;
			}
			else {
#ifdef DEBUG_AUDIO_BUFFER
				printf("AUDIO_BUFFER: cannot alloc space for decoded packet %d\n", pkt->stream_index);
#endif
			}
		}
		else {
#ifdef DEBUG_AUDIO_BUFFER
			printf("AUDIO_BUFFER: cannot decode packet %d\n", pkt->stream_index);
#endif
		}
		av_free(audio_bufQ);
	}
	else {
#ifdef DEBUG_AUDIO_BUFFER
		printf("AUDIO_BUFFER: cannot alloc decode buffer for packet %d\n", pkt->stream_index);
#endif
	}
	return ret; //problems occurred
}

/**
 * removes a packet from the list and returns the next
 * */
AVPacketList *RemoveFromQueue(PacketQueue *q, AVPacketList *p)
{
	AVPacketList *p1;

	if (q->first_pkt == p) {
		q->first_pkt = p->next;
	}
	if (&(p->pkt) == q->minpts_pkt) {
		q->minpts_pkt = NULL;
	}

	AVPacketList *retpk = p->next;
	q->nb_packets--;
	//adjust size here and not in the various cases of the dequeue
	q->size -= p->pkt.size;
	if(&p->pkt)
	{
		av_free_packet(&p->pkt);
	}
	if(p) {
		av_free(p);
	}

	//updating min info
	for (p1 = q->first_pkt; p1; p1 = p1->next) {
		if (!q->minpts_pkt || p1->pkt.pts < q->minpts_pkt->pts) {
			q->minpts_pkt = &(p1->pkt);
		}
	}

	return retpk;
}

AVPacketList *SeekAndDecodePacketStartingFrom(AVPacketList *p, PacketQueue *q, int* size)
{
	while(p) {
			//check if audio packet has been already decoded
			if(p->pkt.convergence_duration == 0) {
				//not decoded yet, try to decode it
				if( !DecodeEnqueuedAudio(&(p->pkt), q, size) ) {
					//it was not possible to decode this packet, return next one
					p = RemoveFromQueue(q, p);
				}
				else
					return p;
			}
			else
				return p;
	}
	return NULL;
}

int PacketQueueGet(PacketQueue *q, AVPacket *pkt, short int av, int* size)
{
	//AVPacket tmp;
	AVPacketList *pkt1 = NULL;
	int ret=-1;
	int SizeToCopy=0;
	int reqsize;

	SDL_LockMutex(q->mutex);

#ifdef DEBUG_QUEUE
	printf("QUEUE: Get NPackets=%d Type=%s\n", q->nb_packets, (q->queueType==AUDIO) ? "AUDIO" : "VIDEO");
#endif

	if((q->queueType==AUDIO && QueueFillingMode) || QueueStopped)
	{
		SDL_UnlockMutex(q->mutex);
		return -1;
	}

	if(av==1) { //somebody requested an audio packet, q is the audio queue
		reqsize = dimAudioQ; //TODO pass this as parameter, not garanteed by SDL to be exactly dimAudioQ
		pkt->size = 0;
		pkt->dts = 0;
		pkt->pts = 0;
		//try to dequeue the first packet of the audio queue
		pkt1 = q->first_pkt;
		while (pkt->size < reqsize && pkt1 && SeekAndDecodePacketStartingFrom(pkt1, q, size)) {
			AVPacketList *next = pkt1->next;	//save it here since we could delete pkt1 later
			if (!pkt->dts) pkt->dts = pkt1->pkt.dts;
			if (!pkt->pts) pkt->pts = pkt1->pkt.pts;
			pkt->stream_index = pkt1->pkt.stream_index;
			pkt->flags = 1;
			pkt->pos = -1;
			pkt->convergence_duration = -1;
			if (pkt1->pkt.size - AudioQueueOffset <= reqsize - pkt->size) { //we need the whole packet
				SizeToCopy = pkt1->pkt.size - AudioQueueOffset;	//packet might be partial
				memcpy(pkt->data + pkt->size, pkt1->pkt.data + AudioQueueOffset, SizeToCopy);
				pkt->size += SizeToCopy;
				AudioQueueOffset = 0;
				RemoveFromQueue(q, pkt1);
			} else {
				SizeToCopy = reqsize - pkt->size;	//partial packet remains
				memcpy(pkt->data + pkt->size, pkt1->pkt.data + AudioQueueOffset, SizeToCopy);
				pkt->size += SizeToCopy;
				AudioQueueOffset += SizeToCopy;
				pkt1->pkt.dts += SizeToCopy/(dimAudioQ/CurrentAudioSamples)/(CurrentAudioFreq/1000);
				pkt1->pkt.pts += SizeToCopy/(dimAudioQ/CurrentAudioSamples)/(CurrentAudioFreq/1000);
			}

#ifdef DEBUG_AUDIO_BUFFER
			printf("2: idx %d    \taqo %d    \tstc %d    \taqe %f    \tpsz %d\n", pkt1->pkt.stream_index, AudioQueueOffset, SizeToCopy, deltaAudioQError, pkt1->pkt.size);
#endif

			//update index of last frame extracted
			//ChunkerPlayerStats_UpdateAudioLossHistory(&(q->PacketHistory), pkt->stream_index, q->last_frame_extracted);
			q->last_frame_extracted = pkt->stream_index;

			pkt1 = next;
		}
		ret = 1; //TODO: check some conditions
	} else { //somebody requested a video packet, q is the video queue
		pkt1 = q->first_pkt;
		if(pkt1) {
#ifdef DEBUG_QUEUE_DEEP
			printf("  AV not 1\n");
#endif
			pkt->size = pkt1->pkt.size;
			pkt->dts = pkt1->pkt.dts;
			pkt->pts = pkt1->pkt.pts;
			pkt->stream_index = pkt1->pkt.stream_index;
			pkt->flags = pkt1->pkt.flags;
			pkt->pos = pkt1->pkt.pos;
			pkt->convergence_duration = pkt1->pkt.convergence_duration;
			//*pkt = pkt1->pkt;
			
			if((pkt->data != NULL) && (pkt1->pkt.data != NULL))
				memcpy(pkt->data, pkt1->pkt.data, pkt1->pkt.size);
				
			//HINT SEE BEFORE q->size -= pkt1->pkt.size;
			RemoveFromQueue(q, pkt1);

			ret = 1;
			
			ChunkerPlayerStats_UpdateVideoLossHistory(&(q->PacketHistory), pkt->stream_index, q->last_frame_extracted);
			
			//update index of last frame extracted
			q->last_frame_extracted = pkt->stream_index;
			last_video_frame_extracted = q->last_frame_extracted;
		}
#ifdef DEBUG_QUEUE
		else {
			printf("  VIDEO pk1 NULL!!!!\n");
		}
#endif
	}

	if(q->nb_packets==0 && q->queueType==AUDIO) {
		QueueFillingMode=1;
#ifdef DEBUG_QUEUE
		printf("QUEUE: Get FillingMode ON\n");
#endif
	}
#ifdef DEBUG_QUEUE
	printf("QUEUE: Get Last %s Frame Extracted = %d\n", (q->queueType==AUDIO) ? "AUDIO" : "VIDEO", q->last_frame_extracted);
#endif

	SDL_UnlockMutex(q->mutex);
	return ret;
}

int AudioDecodeFrame(uint8_t *audio_buf, int buf_size) {
	//struct timeval now;
	int audio_pkt_size = 0;
	int compressed_size = 0;
	long long Now;
	short int DecodeAudio=0, SkipAudio=0;
	//int len1, data_size;

	//gettimeofday(&now,NULL);
	//Now = (now.tv_sec)*1000+now.tv_usec/1000;
	Now=(long long)SDL_GetTicks();
	struct timeval now_tv;

	if(QueueFillingMode || QueueStopped)
	{
		//SDL_LockMutex(timing_mutex);
		FirstTimeAudio=1;
		FirstTime = 1;
		//SDL_UnlockMutex(timing_mutex);
		return -1;
	}

	if((FirstTime==1 || FirstTimeAudio==1) && audioq.size>0) {
		if(audioq.first_pkt->pkt.pts>0)
		{
			//SDL_LockMutex(timing_mutex);
			DeltaTime=Now-(long long)(audioq.first_pkt->pkt.pts);
			FirstTimeAudio = 0;
			FirstTime = 0;
			//SDL_UnlockMutex(timing_mutex);
#ifdef DEBUG_AUDIO 
		 	printf("AUDIO: audio_decode_frame - DeltaTimeAudio=%lld\n",DeltaTime);
#endif
		}
	}

#ifdef DEBUG_AUDIO 
	if(audioq.first_pkt)
	{
		printf("AUDIO: audio_decode_frame - Syncro params: Delta:%lld Now:%lld pts=%lld pts+Delta=%lld ",(long long)DeltaTime,Now,(long long)audioq.first_pkt->pkt.pts,(long long)audioq.first_pkt->pkt.pts+DeltaTime);
		printf("AUDIO: QueueLen=%d ",(int)audioq.nb_packets);
		printf("AUDIO: QueueSize=%d\n",(int)audioq.size);
	}
	else
		printf("AUDIO: audio_decode_frame - Empty queue\n");
#endif

	gettimeofday(&now_tv, NULL);
	if(audioq.nb_packets>0)
	{
		if((double)audioq.first_pkt->pkt.pts+DeltaTime<Now+deltaAudioQ)	//too late ... TODO: figure out the right number
		{
			SkipAudio = 1;
			DecodeAudio = 0;
		}
		else if((double)audioq.first_pkt->pkt.pts+DeltaTime>=Now+deltaAudioQ &&	//TODO: figure out the right number
			(double)audioq.first_pkt->pkt.pts+DeltaTime<=Now+deltaAudioQ+3*deltaAudioQ) {	//TODO: how much in future? On some systems, SDL asks for more buffers in a raw
				SkipAudio = 0;
				DecodeAudio = 1;
		}
	}
	
	while(SkipAudio==1 && audioq.size>0)
	{
		SkipAudio = 0;
#ifdef DEBUG_AUDIO
 		printf("AUDIO: skipaudio: queue size=%d\n",audioq.size);
#endif
		if(PacketQueueGet(&audioq,&AudioPkt,1, &compressed_size) < 0) {
			return -1;
		}
		if(audioq.first_pkt)
		{
			ChunkerPlayerStats_UpdateAudioSkipHistory(&(audioq.PacketHistory), AudioPkt.stream_index, compressed_size);
			
			if((double)audioq.first_pkt->pkt.pts+DeltaTime<Now+deltaAudioQ)	//TODO: figure out the right number
			{
				SkipAudio = 1;
				DecodeAudio = 0;
			}
			else if((double)audioq.first_pkt->pkt.pts+DeltaTime>=Now+deltaAudioQ &&	//TODO: figure out the right number
				(double)audioq.first_pkt->pkt.pts+DeltaTime<=Now+deltaAudioQ+3*deltaAudioQ) {	//TODO: how much in future?
					SkipAudio = 0;
					DecodeAudio = 1;
			}
		}
	}
	if(DecodeAudio==1) {
		if(PacketQueueGet(&audioq,&AudioPkt,1, &compressed_size) < 0) {
			return -1;
		}
		memcpy(audio_buf,AudioPkt.data,AudioPkt.size);
		audio_pkt_size = AudioPkt.size;
#ifdef DEBUG_AUDIO
 		printf("AUDIO: Decode audio\n");
#endif

		ChunkerPlayerStats_UpdateAudioPlayedHistory(&(audioq.PacketHistory), AudioPkt.stream_index, compressed_size);
	}

	return audio_pkt_size;
}

// Render a Frame to a YUV Overlay. Note that the Overlay is already bound to an SDL Surface
// Note that width, height would not be needed in new ffmpeg versions where this info is contained in AVFrame
// see: [FFmpeg-devel] [PATCH] lavc: add width and height fields to AVFrame
int RenderFrame2Overlay(AVFrame *pFrame, int frame_width, int frame_height, SDL_Overlay *YUVOverlay)
{
	AVPicture pict;
	struct SwsContext *img_convert_ctx = NULL;

					if(SDL_LockYUVOverlay(YUVOverlay) < 0) {
						return -1;
					}

					pict.data[0] = YUVOverlay->pixels[0];
					pict.data[1] = YUVOverlay->pixels[2];
					pict.data[2] = YUVOverlay->pixels[1];

					pict.linesize[0] = YUVOverlay->pitches[0];
					pict.linesize[1] = YUVOverlay->pitches[2];
					pict.linesize[2] = YUVOverlay->pitches[1];

					if(img_convert_ctx == NULL) {
						img_convert_ctx = sws_getContext(frame_width, frame_height, PIX_FMT_YUV420P, YUVOverlay->w, YUVOverlay->h, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
						if(img_convert_ctx == NULL) {
							fprintf(stderr, "Cannot initialize the conversion context!\n");
							exit(1);
						}
					}

					// let's draw the data (*yuv[3]) on a SDL screen (*screen)
					sws_scale(img_convert_ctx, pFrame->data, pFrame->linesize, 0, frame_height, pict.data, pict.linesize);
					SDL_UnlockYUVOverlay(YUVOverlay);

	return 0;
}

// Render a YUV Overlay to the specified Rect of the Surface. Note that the Overlay is already bound to an SDL Surface.
int RenderOverlay2Rect(SDL_Overlay *YUVOverlay, SDL_Rect *Rect)
{

					// Lock SDL_yuv_overlay
					if(SDL_MUSTLOCK(MainScreen)) {
						if(SDL_LockSurface(MainScreen) < 0) {
							return -1;
						}
					}

					// Show, baby, show!
					SDL_LockMutex(OverlayMutex);
					SDL_DisplayYUVOverlay(YUVOverlay, Rect);
					SDL_UnlockMutex(OverlayMutex);

					if(SDL_MUSTLOCK(MainScreen)) {
						SDL_UnlockSurface(MainScreen);
					}

	return 0;

}


int VideoCallback(void *valthread)
{
	//AVPacket pktvideo;
	AVCodecContext  *pCodecCtx;
	AVCodec         *pCodec;
	AVFrame         *pFrame;
	int frameFinished;
	long long Now;
	short int SkipVideo, DecodeVideo;
	uint64_t last_pts = 0;
	
#ifdef SAVE_YUV
	static AVFrame* lastSavedFrameBuffer = NULL;
	
	if(!lastSavedFrameBuffer)
		lastSavedFrameBuffer = (AVFrame*) malloc(sizeof(AVFrame));
#endif

	//double frame_rate = 0.0,time_between_frames=0.0;
	//struct timeval now;

	//int wait_for_sync = 1;
	ThreadVal *tval;
	tval = (ThreadVal *)valthread;

	//frame_rate = tval->framerate;
	//time_between_frames = 1.e6 / frame_rate;
	//gettimeofday(&time_now,0);

	//frecon = fopen("recondechunk.mpg","wb");

	//setup video decoder
	pCodec = avcodec_find_decoder_by_name(tval->video_codec);
	if (pCodec) {
		fprintf(stderr, "INIT: Setting VIDEO codecID to: %d\n",pCodec->id);
	} else {
		fprintf(stderr, "INIT: Unknown VIDEO codec: %s!\n", tval->video_codec);
		return -1; // Codec not found
	}

	pCodecCtx=avcodec_alloc_context();
	pCodecCtx->codec_type = CODEC_TYPE_VIDEO;
	//pCodecCtx->debug = FF_DEBUG_DCT_COEFF;
	pCodecCtx->codec_id = pCodec->id;

	//pCodecCtx->bit_rate = 400000;
	// resolution must be a multiple of two
	pCodecCtx->width = tval->width;//176;//352;
	pCodecCtx->height = tval->height;//144;//288;

	// frames per second
	//pCodecCtx->time_base = (AVRational){1,25};
	//pCodecCtx->gop_size = 10; // emit one intra frame every ten frames
	//pCodecCtx->max_b_frames=1;
	pCodecCtx->pix_fmt = PIX_FMT_YUV420P;
	pCodec=avcodec_find_decoder(pCodecCtx->codec_id);

	if(pCodec==NULL) {
		fprintf(stderr, "Unsupported codec!\n");
		return -1; // Codec not found
	}
	if(avcodec_open(pCodecCtx, pCodec) < 0) {
		fprintf(stderr, "could not open codec\n");
		return -1; // Could not open codec
	}
	pFrame=avcodec_alloc_frame();
	if(pFrame==NULL) {
		printf("Memory error!!!\n");
		return -1;
	}
	
#ifdef DEBUG_VIDEO
 	printf("VIDEO: video_callback entering main cycle\n");
#endif

	struct timeval now_tv;
	while(AVPlaying && !quit) {
		if(QueueFillingMode || QueueStopped)
		{
			//SDL_LockMutex(timing_mutex);
			FirstTime = 1;
			//SDL_UnlockMutex(timing_mutex);
			usleep(5000);
			continue;
		}

		DecodeVideo = 0;
		SkipVideo = 0;
		Now=(long long)SDL_GetTicks();
		if(FirstTime==1 && videoq.size>0) {
			if(videoq.first_pkt->pkt.pts>0)
			{
				//SDL_LockMutex(timing_mutex);
				DeltaTime=Now-(long long)videoq.first_pkt->pkt.pts;
				FirstTime = 0;
				//SDL_UnlockMutex(timing_mutex);
			}
#ifdef DEBUG_VIDEO 
		 	printf("VIDEO: VideoCallback - DeltaTimeAudio=%lld\n",DeltaTime);
#endif
		}

#ifdef DEBUG_VIDEO 
		if(videoq.first_pkt)
		{
			printf("VIDEO: VideoCallback - Syncro params: Delta:%lld Now:%lld pts=%lld pts+Delta=%lld ",(long long)DeltaTime,Now,(long long)videoq.first_pkt->pkt.pts,(long long)videoq.first_pkt->pkt.pts+DeltaTime);
			printf("VIDEO: Index=%d ", (int)videoq.first_pkt->pkt.stream_index);
			printf("VIDEO: QueueLen=%d ", (int)videoq.nb_packets);
			printf("VIDEO: QueueSize=%d\n", (int)videoq.size);
		}
		else
			printf("VIDEO: VideoCallback - Empty queue\n");
#endif

		if(videoq.nb_packets>0) {
			long long target_ts = videoq.minpts_pkt->pts + DeltaTime;
			long long frame_timespan = MAX_TOLLERANCE;	//TODO: calculate real value
			if(target_ts<Now-(long long)MAX_TOLLERANCE) {
				SkipVideo = 1;
				DecodeVideo = 0;
			} else if(target_ts>=Now-(long long)MAX_TOLLERANCE && target_ts<=Now) {
				SkipVideo = 0;
				DecodeVideo = 1;
			} else if (last_pts+frame_timespan+DeltaTime<=Now) {
				SkipVideo = 0;
				DecodeVideo = 1;
			}
		}
		// else (i.e. videoq.minpts_pkt->pts+DeltaTime>Now+MAX_TOLLERANCE)
		// do nothing and continue
#ifdef DEBUG_VIDEO
		printf("VIDEO: skipvideo:%d decodevideo:%d\n",SkipVideo,DecodeVideo);
#endif
		gettimeofday(&now_tv, NULL);
		
		if(SkipVideo==1 && videoq.size>0)
		{
			SkipVideo = 0;
#ifdef DEBUG_VIDEO 
 			printf("VIDEO: Skip Video\n");
#endif
			if(PacketQueueGet(&videoq,&VideoPkt,0, NULL) < 0) {
				break;
			}

			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &VideoPkt);
			
			// sometimes assertion fails, maybe the decoder change the frame type
			//~ if(LastSourceIFrameDistance == 0)
				//~ assert(pFrame->pict_type == 1);

			
			ChunkerPlayerStats_UpdateVideoSkipHistory(&(videoq.PacketHistory), VideoPkt.stream_index, pFrame->pict_type, VideoPkt.size, pFrame);
			
			/*if(pFrame->pict_type == 1)
			{
				int i1;
				// every 23 items (23 is the qstride field in the AVFrame struct) there is 1 zero.
				// 396/23 = 17 => 396 macroblocks + 17 zeros = 413 items
				for(i1=0; i1< 413; i1++)
					fprintf(qscaletable_file, "%d\t", (int)pFrame->qscale_table[i1]);
				fprintf(qscaletable_file, "\n");
			}*/
			
			//ChunkerPlayerStats_UpdateVideoPlayedHistory(&(videoq.PacketHistory), VideoPkt.stream_index, pFrame->pict_type, VideoPkt.size);
			continue;
		}

		if (DecodeVideo==1) {
			if(PacketQueueGet(&videoq,&VideoPkt,0, NULL) > 0) {
				avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &VideoPkt);
				last_pts = pFrame->pkt_pts;
				if (!videoq.minpts_pkt || videoq.minpts_pkt->pts > Now - DeltaTime) {
					DecodeVideo = 0;
				}

				if(frameFinished)
				{ // it must be true all the time else error
#ifdef DEBUG_VIDEO
					printf("VIDEO: FrameFinished\n");
#endif
					decoded_vframes++;
					

#ifdef VIDEO_DEINTERLACE
					avpicture_deinterlace(
						(AVPicture*) pFrame,
						(const AVPicture*) pFrame,
						pCodecCtx->pix_fmt,
						tval->width, tval->height);
#endif

					// sometimes assertion fails, maybe the decoder change the frame type
					//~ if(LastSourceIFrameDistance == 0)
						//~ assert(pFrame->pict_type == 1);
#ifdef SAVE_YUV
					if(LastSavedVFrame == -1)
					{
						memcpy(lastSavedFrameBuffer, pFrame, sizeof(AVFrame));
						SaveFrame(pFrame, pCodecCtx->width, pCodecCtx->height);
						LastSavedVFrame = VideoPkt.stream_index;
					}
					else if(LastSavedVFrame == (VideoPkt.stream_index-1))
					{
						memcpy(lastSavedFrameBuffer, pFrame, sizeof(AVFrame));
						SaveFrame(pFrame, pCodecCtx->width, pCodecCtx->height);
						LastSavedVFrame = VideoPkt.stream_index;
					}
					else if(LastSavedVFrame >= 0)
					{
						while(LastSavedVFrame < (VideoPkt.stream_index-1))
						{
							SaveFrame(lastSavedFrameBuffer, pCodecCtx->width, pCodecCtx->height);
						}

						memcpy(lastSavedFrameBuffer, pFrame, sizeof(AVFrame));
						SaveFrame(pFrame, pCodecCtx->width, pCodecCtx->height);
						LastSavedVFrame = VideoPkt.stream_index;
					}
#endif
					ChunkerPlayerStats_UpdateVideoPlayedHistory(&(videoq.PacketHistory), VideoPkt.stream_index, pFrame->pict_type, VideoPkt.size, pFrame);

					if(SilentMode)
						continue;


					if (RenderFrame2Overlay(pFrame, pCodecCtx->width, pCodecCtx->height, YUVOverlay) < 0){
						continue;
					}

					if (RenderOverlay2Rect(YUVOverlay, &OverlayRect) < 0) {
						continue;
					}

					//redisplay logo
					/**SDL_BlitSurface(image, NULL, MainScreen, &dest);*/
					/* Update the screen area just changed */
					/**SDL_UpdateRects(MainScreen, 1, &dest);*/
				} //if FrameFinished
				else
				{
					ChunkerPlayerStats_UpdateVideoLossHistory(&(videoq.PacketHistory), VideoPkt.stream_index+1, videoq.last_frame_extracted-1);
				}
			} else { // if packet_queue_get
				DecodeVideo = 0;
			}
		} //if DecodeVideo=1

		usleep(5000);
	}
	avcodec_close(pCodecCtx);
	av_free(pCodecCtx);
	av_free(pFrame);
	//fclose(frecon);
#ifdef DEBUG_VIDEO
 	printf("VIDEO: video callback end\n");
#endif

#ifdef SAVE_YUV
	if(!lastSavedFrameBuffer)
		free(lastSavedFrameBuffer);
	
	lastSavedFrameBuffer = NULL;
#endif

	return 0;
}

void AudioCallback(void *userdata, Uint8 *stream, int len)
{
	//AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
	int audio_size;

	static uint8_t audio_buf[AVCODEC_MAX_AUDIO_FRAME_SIZE];

	memset(audio_buf, CurrentAudioSilence, sizeof(audio_buf));
	audio_size = AudioDecodeFrame(audio_buf, sizeof(audio_buf));
	
	if(SilentMode < 2) {
		if(audio_size != len) {
			memset(stream, CurrentAudioSilence, len);
		} else {
			memcpy(stream, (uint8_t *)audio_buf, len);
		}
	}
}

void SaveFrame(AVFrame *pFrame, int width, int height)
{
	FILE *pFile;
	int  y;
  
	 // Open file
	pFile=fopen(YUVFileName, "ab");
	if(pFile==NULL)
		return;
  
	// Write header
	//fprintf(pFile, "P5\n%d %d\n255\n", width, height);
  
	// Write Y data
	for(y=0; y<height; y++)
  		fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width, pFile);
	// Write U data
	for(y=0; y<height/2; y++)
  		fwrite(pFrame->data[1]+y*pFrame->linesize[1], 1, width/2, pFile);
	// Write V data
	for(y=0; y<height/2; y++)
  		fwrite(pFrame->data[2]+y*pFrame->linesize[2], 1, width/2, pFile);
  
	// Close file
	fclose(pFile);
}

int ChunkerPlayerCore_IsRunning()
{
	return AVPlaying;
}

void ChunkerPlayerCore_Play()
{
	if(AVPlaying) return;
	AVPlaying = 1;
	
	SDL_PauseAudio(0);
	video_thread = SDL_CreateThread(VideoCallback, &VideoCallbackThreadParams);
	ChunkerPlayerStats_Init(&VideoCallbackThreadParams);
	stats_thread = SDL_CreateThread(CollectStatisticsThread, NULL);
	
	decoded_vframes = 0;
	LastSavedVFrame = -1;
}

void ChunkerPlayerCore_Stop()
{
	if(!AVPlaying) return;
	
	AVPlaying = 0;
	
	// Stop audio&video playback
	SDL_WaitThread(video_thread, NULL);
	SDL_WaitThread(stats_thread, NULL);
	SDL_PauseAudio(1);	
	
	if(YUVOverlay != NULL)
	{
		SDL_FreeYUVOverlay(YUVOverlay);
		YUVOverlay = NULL;
	}
	
	PacketQueueReset(&audioq);
	PacketQueueReset(&videoq);
	
	avcodec_close(aCodecCtx);
	av_free(aCodecCtx);
	free(AudioPkt.data);
	free(VideoPkt.data);
	free(outbuf_audio);
	
	/*
	* Sleep two buffers' worth of audio before closing, in order
	*  to allow the playback to finish. This isn't always enough;
	*   perhaps SDL needs a way to explicitly wait for device drain?
	*/
	int delay = 2 * 1000 * CurrentAudioSamples / CurrentAudioFreq;
	// printf("SDL_Delay(%d)\n", delay*10);
	SDL_Delay(delay*10);
}

void ChunkerPlayerCore_Finalize()
{
	if(YUVOverlay != NULL)
	{
		SDL_FreeYUVOverlay(YUVOverlay);
		YUVOverlay = NULL;
	}

	SDL_CloseAudio();
}

void ChunkerPlayerCore_Pause()
{
	if(!AVPlaying) return;
	
	AVPlaying = 0;
	
	// Stop audio&video playback
	SDL_WaitThread(video_thread, NULL);
	SDL_PauseAudio(1);
	
	PacketQueueReset(&audioq);
	PacketQueueReset(&videoq);
}

int ChunkerPlayerCore_AudioEnded()
{
	return (audioq.nb_packets==0 && audioq.last_frame_extracted>0);
}

void ChunkerPlayerCore_ResetAVQueues()
{
#ifdef DEBUG_QUEUE
	printf("QUEUE: MAIN SHOULD RESET\n");
#endif
	PacketQueueReset(&audioq);
	PacketQueueReset(&videoq);
}

int ChunkerPlayerCore_EnqueueBlocks(const uint8_t *block, const int block_size)
{
#ifdef EMULATE_CHUNK_LOSS
	static time_t loss_cycle_start_time = 0, now = 0;
	static int early_losses = 0;
	static int clp_frames = 0;
	
	if(ScheduledChunkLosses)
	{
		static unsigned int random_threshold;
		now=time(NULL);
		if(!loss_cycle_start_time)
			loss_cycle_start_time = now;
			
		if(((now-loss_cycle_start_time) >= ScheduledChunkLosses[((CurrChunkLossIndex+1)%NScheduledChunkLosses)].Time) && (NScheduledChunkLosses>1 || CurrChunkLossIndex==-1))
		{
			CurrChunkLossIndex = ((CurrChunkLossIndex+1)%NScheduledChunkLosses);
			if(CurrChunkLossIndex == (NScheduledChunkLosses-1))
				loss_cycle_start_time = now;
			
			if(ScheduledChunkLosses[CurrChunkLossIndex].Value == -1)
				random_threshold = ScheduledChunkLosses[CurrChunkLossIndex].MinValue + (rand() % (ScheduledChunkLosses[CurrChunkLossIndex].MaxValue-ScheduledChunkLosses[CurrChunkLossIndex].MinValue));
			else
				random_threshold = ScheduledChunkLosses[CurrChunkLossIndex].Value;
			
			printf("new ScheduledChunkLoss, time: %d, value: %d\n", (int)ScheduledChunkLosses[CurrChunkLossIndex].Time, random_threshold);
		}
	
		if(clp_frames > 0)
		{
			clp_frames--;
			return PLAYER_FAIL_RETURN;
		}
		if((rand() % 100) < random_threshold)
		{
			if(early_losses > 0)
                early_losses--;
            else
            {
                clp_frames=early_losses=(ScheduledChunkLosses[CurrChunkLossIndex].Burstiness-1);
                return PLAYER_FAIL_RETURN;
            }
		}
	}
#endif

	Chunk *gchunk = NULL;
	int decoded_size = -1;
	uint8_t *tempdata, *buffer;
	int j;
	Frame *frame = NULL;
	AVPacket packet, packetaudio;

	uint16_t *audio_bufQ = NULL;

	//the frame.h gets encoded into 5 slots of 32bits (3 ints plus 2 more for the timeval struct
	static int sizeFrameHeader = 5*sizeof(int32_t);
	//the following we dont need anymore
	//static int ExternalChunk_header_size = 5*CHUNK_TRANSCODING_INT_SIZE + 2*CHUNK_TRANSCODING_INT_SIZE + 2*CHUNK_TRANSCODING_INT_SIZE + 1*CHUNK_TRANSCODING_INT_SIZE*2;

	static int chunks_out_of_order = 0;
	static int last_chunk_id = -1;

	audio_bufQ = (uint16_t *)av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
	if(!audio_bufQ) {
		printf("Memory error in audio_bufQ!\n");
		return PLAYER_FAIL_RETURN;
	}

	gchunk = (Chunk *)malloc(sizeof(Chunk));
	if(!gchunk) {
		printf("Memory error in gchunk!\n");
		av_free(audio_bufQ);
		return PLAYER_FAIL_RETURN;
	}

	decoded_size = decodeChunk(gchunk, block, block_size);

	if(last_chunk_id == -1)
		last_chunk_id = gchunk->id;

	if(gchunk->id > (last_chunk_id+1)) {
		chunks_out_of_order += gchunk->id - last_chunk_id - 1;
	}
	last_chunk_id = gchunk->id;

#ifdef DEBUG_CHUNKER
	printf("CHUNKER: enqueueBlock: id %d decoded_size %d target size %d - out_of_order %d\n", gchunk->id, decoded_size, GRAPES_ENCODED_CHUNK_HEADER_SIZE + ExternalChunk_header_size + gchunk->size, chunks_out_of_order);
#endif
  if(decoded_size < 0) {
		//HINT here i should differentiate between various return values of the decode
		//in order to free what has been allocated there
		printf("chunk probably corrupted!\n");
		av_free(audio_bufQ);
		free(gchunk);
		return PLAYER_FAIL_RETURN;
	}

	frame = (Frame *)malloc(sizeof(Frame));
	if(!frame) {
		printf("Memory error in Frame!\n");
		if(gchunk) {
			if(gchunk->attributes) {
				free(gchunk->attributes);
			}
			free(gchunk);
		}
		av_free(audio_bufQ);
		return PLAYER_FAIL_RETURN;
	}

	tempdata = gchunk->data; //let it point to first frame of payload
	j=gchunk->size;
	while(j>0 && !quit) {
		frame->number = bit32_encoded_pull(tempdata);
		tempdata += CHUNK_TRANSCODING_INT_SIZE;
		frame->timestamp.tv_sec = bit32_encoded_pull(tempdata);
		tempdata += CHUNK_TRANSCODING_INT_SIZE;
		frame->timestamp.tv_usec = bit32_encoded_pull(tempdata);
		tempdata += CHUNK_TRANSCODING_INT_SIZE;
		frame->size = bit32_encoded_pull(tempdata);
		tempdata += CHUNK_TRANSCODING_INT_SIZE;
		frame->type = bit32_encoded_pull(tempdata);
		tempdata += CHUNK_TRANSCODING_INT_SIZE;

		buffer = tempdata; // here coded frame information
		tempdata += frame->size; //let it point to the next frame

		if(frame->type < 5) { // video frame
			av_init_packet(&packet);
			packet.data = buffer;//video_bufQ;
			packet.size = frame->size;
			packet.pts = frame->timestamp.tv_sec*(unsigned long long)1000+frame->timestamp.tv_usec;
			packet.dts = frame->timestamp.tv_sec*(unsigned long long)1000+frame->timestamp.tv_usec;
			packet.stream_index = frame->number; // use of stream_index for number frame
			//packet.duration = frame->timestamp.tv_sec;
			if(packet.size > 0) {
				int ret = ChunkerPlayerCore_PacketQueuePut(&videoq, &packet); //the _put makes a copy of the packet
				if (ret == 1) {	//TODO: check and correct return values
					fprintf(stderr, "late chunk received, increasing delay\n");
					DeltaTime += 40;	//TODO: handle audio skip; verify this value
				}
			}

#ifdef DEBUG_SOURCE
			printf("SOURCE: Insert video in queue pts=%lld %d %d sindex:%d\n",packet.pts,(int)frame->timestamp.tv_sec,(int)frame->timestamp.tv_usec,packet.stream_index);
#endif
		}
		else if(frame->type == 5) { // audio frame
			av_init_packet(&packetaudio);
			packetaudio.data = buffer;
			packetaudio.size = frame->size;
			packetaudio.pts = frame->timestamp.tv_sec*(unsigned long long)1000+frame->timestamp.tv_usec;
			packetaudio.dts = frame->timestamp.tv_sec*(unsigned long long)1000+frame->timestamp.tv_usec;
			//packetaudio.duration = frame->timestamp.tv_sec;
			packetaudio.stream_index = frame->number; // use of stream_index for number frame
			packetaudio.flags = 1;
			packetaudio.pos = -1;

			//instead of -1, in order to signal it is not decoded yet
			packetaudio.convergence_duration = 0;

			// insert the audio frame into the queue
			if(packetaudio.size > 0) {
				int ret = ChunkerPlayerCore_PacketQueuePut(&audioq, &packetaudio);//makes a copy of the packet so i can free here
				if (ret == 1) {	//TODO: check and correct return values
					fprintf(stderr, "late chunk received, increasing delay\n");
					DeltaTime += 40;	//TODO: handle audio skip; verify this value
				}
			}

#ifdef DEBUG_SOURCE
			printf("SOURCE: Insert audio in queue pts=%lld sindex:%d\n", packetaudio.pts, packetaudio.stream_index);
#endif
		}
		else {
			printf("SOURCE: Unknown frame type %d. Size %d\n", frame->type, frame->size);
		}
		if(frame->size > 0)
			j = j - sizeFrameHeader - frame->size;
		else {
			printf("SOURCE: Corrupt frames (size %d) in chunk. Skipping it...\n", frame->size);
			j = -1;
		}
	}
	//chunk ingestion terminated!
	if(gchunk) {
		if(gchunk->attributes) {
			free(gchunk->attributes);
		}
		if(gchunk->data)
			free(gchunk->data);
		free(gchunk);
	}
	if(frame)
		free(frame);
	if(audio_bufQ)
		av_free(audio_bufQ);
		
	return PLAYER_OK_RETURN;
}

void ChunkerPlayerCore_SetupOverlay(int width, int height)
{
	// if(!MainScreen && !SilentMode)
	// {
		// printf("Cannot find main screen, exiting...\n");
		// exit(1);
	// }
	
	if(SilentMode)
		return;
		
	SDL_LockMutex(OverlayMutex);
	if(YUVOverlay != NULL)
	{
		SDL_FreeYUVOverlay(YUVOverlay);
		YUVOverlay = NULL;
	}
	
	// create video overlay for display of video frames
	// printf("SDL_CreateYUVOverlay(%d, %d, SDL_YV12_OVERLAY, MainScreen)\n", width, height);
	YUVOverlay = SDL_CreateYUVOverlay(width, height, SDL_YV12_OVERLAY, MainScreen);
	// YUVOverlay = SDL_CreateYUVOverlay(OverlayRect.w, OverlayRect.h, SDL_YV12_OVERLAY, MainScreen);
	if ( YUVOverlay == NULL )
	{
		fprintf(stderr,"SDL: Couldn't create SDL_yuv_overlay: %s", SDL_GetError());
		exit(1);
	}

	if ( YUVOverlay->hw_overlay )
		fprintf(stderr,"SDL: Using hardware overlay.\n");
	// OverlayRect.x = (screen_w - width) / 2;
	
	SDL_DisplayYUVOverlay(YUVOverlay, &OverlayRect);
	
	SDL_UnlockMutex(OverlayMutex);
}

int CollectStatisticsThread(void *params)
{
	struct timeval last_stats_evaluation, now, last_trace, last_qoe_evaluation;
	gettimeofday(&last_stats_evaluation, NULL);
	last_trace = last_stats_evaluation;
	last_qoe_evaluation = last_stats_evaluation;
	
	double video_qdensity;
	double audio_qdensity;
	char audio_stats_text[255];
	char video_stats_text[255];
	SStats audio_statistics, video_statistics;
	double qoe = 0;
	int sleep_time = STATS_THREAD_GRANULARITY*1000;
	int audio_avg_bitrate = 0;
	int video_avg_bitrate = 0;
	
	while(AVPlaying && !quit)
	{
		usleep(sleep_time);
		
		gettimeofday(&now, NULL);
		
		if((((now.tv_sec*1000)+(now.tv_usec/1000)) - ((last_stats_evaluation.tv_sec*1000)+(last_stats_evaluation.tv_usec/1000))) > GUI_PRINTSTATS_INTERVAL)
		{
			// estimate audio queue stats
			int audio_stats_changed = ChunkerPlayerStats_GetStats(&(audioq.PacketHistory), &audio_statistics);
			
			// estimate video queue stats
			int video_stats_changed = ChunkerPlayerStats_GetStats(&(videoq.PacketHistory), &video_statistics);

			// compute avg bitrate up to now
			audioq.cumulative_bitrate += audio_statistics.Bitrate;
			audioq.cumulative_samples++;
			audio_avg_bitrate = (int)( ((double)audioq.cumulative_bitrate) / ((double)audioq.cumulative_samples) );
			videoq.cumulative_bitrate += video_statistics.Bitrate;
			videoq.cumulative_samples++;
			video_avg_bitrate = (int)( ((double)videoq.cumulative_bitrate) / ((double)videoq.cumulative_samples) );

#ifdef DEBUG_STATS
			printf("VIDEO: %d Kbit/sec; ", video_statistics.Bitrate);
			printf("AUDIO: %d Kbit/sec\n", audio_statistics.Bitrate);
#endif

			// QUEUE DENSITY EVALUATION
			if((audioq.last_pkt != NULL) && (audioq.first_pkt != NULL))
				if(audioq.last_pkt->pkt.stream_index >= audioq.first_pkt->pkt.stream_index)
				{
					//plus 1 because if they are adjacent (difference 1) there really should be 2 packets in the queue
					audio_qdensity = (double)audioq.nb_packets / (double)(audioq.last_pkt->pkt.stream_index - audioq.first_pkt->pkt.stream_index + 1) * 100.0;
				}
			
			if((videoq.last_pkt != NULL) && (videoq.first_pkt != NULL))
				if(videoq.last_pkt->pkt.stream_index >= videoq.first_pkt->pkt.stream_index)
				{
					// plus 1 because if they are adjacent (difference 1) there really should be 2 packets in the queue
					video_qdensity = (double)videoq.nb_packets / (double)(videoq.last_pkt->pkt.stream_index - videoq.first_pkt->pkt.stream_index + 1) * 100.0;
				}
			
			if(LogTraces)
			{
				ChunkerPlayerStats_PrintHistoryTrace(&(audioq.PacketHistory), AudioTraceFilename);
				ChunkerPlayerStats_PrintHistoryTrace(&(videoq.PacketHistory), VideoTraceFilename);
				
				//if(SilentMode != 1 && SilentMode != 2)
					ChunkerPlayerStats_PrintContextFile();
			}

			// PRINT STATISTICS ON GUI
			if(!Audio_ON)
				sprintf(audio_stats_text, "AUDIO MUTED");
			else if(audio_stats_changed)
//				sprintf(audio_stats_text, "[AUDIO] qsize: %d qdensity: %d\%% - losses: %d/sec (%ld tot) - skips: %d/sec (%ld tot)", (int)audioq.nb_packets, (int)audio_qdensity, (int)audio_statistics.Lossrate, audioq.PacketHistory.LostCount, audio_statistics.Skiprate, audioq.PacketHistory.SkipCount);
				sprintf(audio_stats_text, "[AUDIO] qsize: %d qdensity: %d\%% - losses: %d/sec (%ld tot) - rate: %d kbits/sec (avg: %d)", (int)audioq.nb_packets, (int)audio_qdensity, (int)audio_statistics.Lossrate, audioq.PacketHistory.LostCount, audio_statistics.Bitrate, audio_avg_bitrate);
			else
				sprintf(audio_stats_text, "waiting for incoming audio packets...");

			if(video_stats_changed)
			{
				char est_psnr_string[255];
				sprintf(est_psnr_string, ".");
				if(qoe)
				{
					sprintf(est_psnr_string, " - Est. Mean PSNR: %.1f db", (float)qoe);
#ifdef PSNR_PUBLICATION
					// Publish measure into repository
					if(RepoAddress[0]!='\0')
					{
					    MeasurementRecord r;
	                    r.originator = NetworkID;
	                    r.targetA = NetworkID;
	                    r.targetB = NULL;
	                    r.published_name = "PSNR_MEAN";
	                    r.value = qoe;
	                    r.string_value = NULL;
	                    r.channel = Channels[SelectedChannel].Title;
	                    gettimeofday(&(r.timestamp), NULL);
	                    // One update every REPO_UPDATE_INTERVALL seconds
	                    struct timeval ElapsedTime;
	                    timeval_subtract(&(r.timestamp),&LastTimeRepoPublish,&ElapsedTime);
                        if(ElapsedTime.tv_sec>=PSNR_REPO_UPDATE_INTERVALL)
                        {
                            LastTimeRepoPublish=r.timestamp;
                            if(repPublish(repoclient,NULL,NULL,&r)!=NULL) {
#ifdef DEBUG_PSNR
                               printf("PSNR publish: %s  %e  %s\n",r.originator,qoe,r.channel);
#endif
														}
                        }
                   }
#endif
				}

//				sprintf(video_stats_text, "[VIDEO] qsize: %d qdensity: %d\%% - losses: %d/sec (%ld tot) - skips: %d/sec (%ld tot)%s", (int)videoq.nb_packets, (int)video_qdensity, video_statistics.Lossrate, videoq.PacketHistory.LostCount, video_statistics.Skiprate, videoq.PacketHistory.SkipCount, est_psnr_string);
				sprintf(video_stats_text, "[VIDEO] qsize: %d qdensity: %d\%% - losses: %d/sec (%ld tot) - rate: %d kbits/sec (avg: %d) %s", (int)videoq.nb_packets, (int)video_qdensity, video_statistics.Lossrate, videoq.PacketHistory.LostCount, video_statistics.Bitrate, video_avg_bitrate, est_psnr_string);
			}
			else
				sprintf(video_stats_text, "waiting for incoming video packets...");
			
			if(qoe)
			    ChunkerPlayerGUI_SetStatsText(audio_stats_text, video_stats_text,(qoe>LED_THRS_YELLOW?LED_GREEN:((qoe<=LED_THRS_YELLOW && qoe>LED_THRS_RED)?LED_YELLOW:LED_RED)));
			else
			    ChunkerPlayerGUI_SetStatsText(audio_stats_text, video_stats_text,LED_GREEN);
			

			last_stats_evaluation = now;
		}
		
		if((((now.tv_sec*1000)+(now.tv_usec/1000)) - ((last_qoe_evaluation.tv_sec*1000)+(last_qoe_evaluation.tv_usec/1000))) > EVAL_QOE_INTERVAL)
		{
			// ESTIMATE QoE
			//ChunkerPlayerStats_GetMeanVideoQuality(&(videoq.PacketHistory), &qoe);
			// ESTIMATE QoE using real-time computed cumulative average bitrate
			// (plus a diminshing contribution of the instantaneous bitrate, until the cumulative avg stabilizes)
			int input_bitrate = 0;
			// stabilize after circa 30 seconds
			if(videoq.cumulative_samples < 30*(1000/GUI_PRINTSTATS_INTERVAL))
				input_bitrate = video_statistics.Bitrate;
			else
				input_bitrate = video_avg_bitrate;
			//double a = 1 / ((double)videoq.cumulative_samples);
			//double b = 1-a;
			//double input_bitrate = a*((double)video_statistics.Bitrate) + b*((double)video_avg_bitrate);
			ChunkerPlayerStats_GetMeanVideoQuality(&(videoq.PacketHistory), input_bitrate, &qoe);
#ifdef DEBUG_STATS
			printf("rate %d avg %d wghtd %d cum_samp %d PSNR %f\n", video_statistics.Bitrate, video_avg_bitrate, (int)input_bitrate, videoq.cumulative_samples, (float)qoe);
#endif
			last_qoe_evaluation = now;
		}
	}
	return 0;
}
