// player.c
// Author 
// Diego Reforgiato, Dario Marchese, Carmelo Daniele, Giuseppe Tropea
//
// Use the file compile to compile the program to build (assuming libavformat and libavcodec are 
// correctly installed your system).
//
// Run using
//
// player <width> <height>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <SDL.h>
#include <SDL_thread.h>
#include <SDL_mutex.h>
#include <SDL_image.h>
#include <math.h>

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#include "http_default_urls.h"
#include "chunker_player.h"
#include "codec_definitions.h"

#define SDL_AUDIO_BUFFER_SIZE 1024

#define MAX_TOLLERANCE 40
#define AUDIO	1
#define VIDEO	2
#define QUEUE_MAX_SIZE 3000

#define FULLSCREEN_ICON_FILE "icons/fullscreen32.png"
#define NOFULLSCREEN_ICON_FILE "icons/nofullscreen32.png"
#define FULLSCREEN_HOVER_ICON_FILE "icons/fullscreen32.png"
#define NOFULLSCREEN_HOVER_ICON_FILE "icons/nofullscreen32.png"

#define BUTTONS_LAYER_OFFSET 10

typedef enum Status { STOPPED, RUNNING, PAUSED } Status;

//#define DEBUG_AUDIO
//#define DEBUG_VIDEO
#define DEBUG_QUEUE
#define DEBUG_SOURCE
//#define DEBUG_STATS
//#define DEBUG_AUDIO_BUFFER
//#define DEBUG_CHUNKER


short int QueueFillingMode=1;
short int QueueStopped=0;

typedef struct PacketQueue {
	AVPacketList *first_pkt;
	AVPacketList *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	short int queueType;
	int last_frame_extracted; //HINT THIS SHOULD BE MORE THAN 4 BYTES
	int total_lost_frames;
	double density;
} PacketQueue;

typedef struct threadVal {
	int width;
	int height;
	float aspect_ratio;
} ThreadVal;

int AudioQueueOffset=0;
PacketQueue audioq;
PacketQueue videoq;
AVPacket AudioPkt, VideoPkt;
int quit = 0;
int SaveYUV=0;
char YUVFileName[256];

int queue_filling_threshold = 0;

SDL_Surface *screen;
SDL_Overlay *yuv_overlay;
SDL_Rect    rect;
SDL_Rect *initRect = NULL;
SDL_AudioSpec spec;
Status CurrStatus = STOPPED;

struct SwsContext *img_convert_ctx = NULL;
float ratio;

//SDL_mutex *timing_mutex;

int got_sigint = 0;

long long DeltaTime;
short int FirstTimeAudio=1, FirstTime = 1;

int dimAudioQ;
float deltaAudioQ;
float deltaAudioQError=0;

void SaveFrame(AVFrame *pFrame, int width, int height);

// other GUI staff
SDL_Cursor *init_system_cursor(const char *image[]);
void refresh_fullscreen_button(int hover);
void toggle_fullscreen();
void aspect_ratio_resize(float aspect_ratio, int width, int height, int* out_width, int* out_height);
int fullscreen = 0; // fullscreen vs windowized flag
SDL_Rect    fullscreenIconBox;
SDL_Surface *fullscreenIcon;
SDL_Surface *fullscreenHoverIcon;
SDL_Surface *nofullscreenIcon;
SDL_Surface *nofullscreenHoverIcon;
SDL_Cursor *defaultCursor;
SDL_Cursor *handCursor;
int fullscreenButtonHover = 0;
int silentMode = 0;

/* XPM */
static char *handXPM[] = {
/* columns rows colors chars-per-pixel */
"32 32 3 1",
"  c black",
". c gray100",
"X c None",
/* pixels */
"XXXXX  XXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXX .. XXXXXXXXXXXXXXXXXXXXXXXX",
"XXXX .. XXXXXXXXXXXXXXXXXXXXXXXX",
"XXXX .. XXXXXXXXXXXXXXXXXXXXXXXX",
"XXXX .. XXXXXXXXXXXXXXXXXXXXXXXX",
"XXXX ..   XXXXXXXXXXXXXXXXXXXXXX",
"XXXX .. ..   XXXXXXXXXXXXXXXXXXX",
"XXXX .. .. ..  XXXXXXXXXXXXXXXXX",
"XXXX .. .. .. . XXXXXXXXXXXXXXXX",
"   X .. .. .. .. XXXXXXXXXXXXXXX",
" ..  ........ .. XXXXXXXXXXXXXXX",
" ... ........... XXXXXXXXXXXXXXX",
"X .. ........... XXXXXXXXXXXXXXX",
"XX . ........... XXXXXXXXXXXXXXX",
"XX ............. XXXXXXXXXXXXXXX",
"XXX ............ XXXXXXXXXXXXXXX",
"XXX ........... XXXXXXXXXXXXXXXX",
"XXXX .......... XXXXXXXXXXXXXXXX",
"XXXX .......... XXXXXXXXXXXXXXXX",
"XXXXX ........ XXXXXXXXXXXXXXXXX",
"XXXXX ........ XXXXXXXXXXXXXXXXX",
"XXXXX          XXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"0,0"
};

void packet_queue_init(PacketQueue *q, short int Type) {
#ifdef DEBUG_QUEUE
	printf("QUEUE: INIT BEGIN: NPackets=%d Type=%d\n", q->nb_packets, q->queueType);
#endif
	memset(q,0,sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	QueueFillingMode=1;
	q->queueType=Type;
	q->last_frame_extracted = -1;
	q->total_lost_frames = 0;
	q->first_pkt= NULL;
	//q->last_pkt = NULL;
	q->nb_packets = 0;
	q->size = 0;
	q->density= 0.0;
	FirstTime = 1;
	FirstTimeAudio = 1;
#ifdef DEBUG_QUEUE
	printf("QUEUE: INIT END: NPackets=%d Type=%d\n", q->nb_packets, q->queueType);
#endif
}

void packet_queue_reset(PacketQueue *q, short int Type) {
	AVPacketList *tmp,*tmp1;
#ifdef DEBUG_QUEUE
	printf("QUEUE: RESET BEGIN: NPackets=%d Type=%d LastExtr=%d\n", q->nb_packets, q->queueType, q->last_frame_extracted);
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
	}
#ifdef DEBUG_QUEUE
	printf("\n");
#endif

	QueueFillingMode=1;
	q->last_frame_extracted = -1;
	q->total_lost_frames = 0;
	q->first_pkt= NULL;
	//q->last_pkt = NULL;
	q->nb_packets = 0;
	q->size = 0;
	q->density= 0.0;
	FirstTime = 1;
	FirstTimeAudio = 1;
#ifdef DEBUG_QUEUE
	printf("QUEUE: RESET END: NPackets=%d Type=%d LastExtr=%d\n", q->nb_packets, q->queueType, q->last_frame_extracted);
#endif
	SDL_UnlockMutex(q->mutex);
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
	short int skip = 0;
	AVPacketList *pkt1, *tmp, *prevtmp;
/*
	if(q->nb_packets > QUEUE_MAX_SIZE) {
#ifdef DEBUG_QUEUE
		printf("QUEUE: PUT i have TOO MANY packets %d Type=%d\n", q->nb_packets, q->queueType);
#endif    
		return -1;
  }
*/
	//make a copy of the incoming packet
	if(av_dup_packet(pkt) < 0) {
#ifdef DEBUG_QUEUE
		printf("QUEUE: PUT in Queue cannot duplicate in packet	: NPackets=%d Type=%d\n",q->nb_packets, q->queueType);
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

	SDL_LockMutex(q->mutex);

	// INSERTION SORT ALGORITHM
	// before inserting pkt, check if pkt.stream_index is <= current_extracted_frame.
//	if(pkt->stream_index > q->last_frame_extracted) {
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
				printf("QUEUE: PUT: we already have frame with index %d, skipping\n", pkt->stream_index);
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
		}
//	}
/*
	else {
		av_free_packet(&pkt1->pkt);
		av_free(pkt1);
#ifdef DEBUG_QUEUE
				printf("QUEUE: PUT: NOT inserting because index %d > last extracted %d\n", pkt->stream_index, q->last_frame_extracted);
#endif
	}
*/
	if(q->last_pkt->pkt.stream_index > q->first_pkt->pkt.stream_index)
		q->density = (double)q->nb_packets / (double)(q->last_pkt->pkt.stream_index - q->first_pkt->pkt.stream_index) * 100.0;

#ifdef DEBUG_STATS
	if(q->queueType == AUDIO)
		printf("STATS: AUDIO QUEUE DENSITY percentage %f\n", q->density);
	if(q->queueType == VIDEO)
		printf("STATS: VIDEO QUEUE DENSITY percentage %f\n", q->density);
#endif

	SDL_UnlockMutex(q->mutex);
	return 0;
}


int decode_enqueued_audio_packet(AVPacket *pkt, PacketQueue *q) {
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
				//discard the old encoded bytes
				av_free(pkt->data);
				//subtract them from queue size
				q->size -= pkt->size;
				pkt->data = (int8_t *)dataQ;
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


//removes a packet from the list and returns the next
AVPacketList *remove_from_queue(PacketQueue *q, AVPacketList *p) {
	AVPacketList *retpk = p->next;
	q->nb_packets--;
	//adjust size here and not in the various cases of the dequeue
	q->size -= p->pkt.size;
	if(&p->pkt)
		av_free_packet(&p->pkt);
	if(p)
		av_free(p);
	return retpk;
}

AVPacketList *seek_and_decode_packet_starting_from(AVPacketList *p, PacketQueue *q) {
	while(p) {
			//check if audio packet has been already decoded
			if(p->pkt.convergence_duration == 0) {
				//not decoded yet, try to decode it
				if( !decode_enqueued_audio_packet(&(p->pkt), q) ) {
					//it was not possible to decode this packet, return next one
					p = remove_from_queue(q, p);
				}
				else
					return p;
			}
			else
				return p;
	}
	return NULL;
}

void update_queue_stats(PacketQueue *q, int packet_index) {
	double percentage = 0.0;	
	//compute lost frame statistics
	if(q->last_frame_extracted > 0 && packet_index > q->last_frame_extracted) {
		q->total_lost_frames = q->total_lost_frames + packet_index - q->last_frame_extracted - 1;
		percentage = (double)q->total_lost_frames / (double)q->last_frame_extracted * 100.0;
#ifdef DEBUG_STATS
		if(q->queueType == AUDIO)
			printf("STATS: AUDIO FRAMES LOST: total %d percentage %f\n", q->total_lost_frames, percentage);
		else if(q->queueType == VIDEO)
			printf("STATS: VIDEO FRAMES LOST: total %d percentage %f\n", q->total_lost_frames, percentage);
#endif
	}
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt, short int av) {
	//AVPacket tmp;
	AVPacketList *pkt1 = NULL;
	int ret=-1;
	int SizeToCopy=0;

	SDL_LockMutex(q->mutex);

#ifdef DEBUG_QUEUE
	printf("QUEUE: Get NPackets=%d Type=%d\n", q->nb_packets, q->queueType);
#endif

	if((q->queueType==AUDIO && QueueFillingMode) || QueueStopped)
	{
		SDL_UnlockMutex(q->mutex);
		return -1;
	}

	if(av==1) { //somebody requested an audio packet, q is the audio queue
		//try to dequeue the first packet of the audio queue
		pkt1 = seek_and_decode_packet_starting_from(q->first_pkt, q);
		if(pkt1) { //yes we have them!
			if(pkt1->pkt.size-AudioQueueOffset > dimAudioQ) {
				//one packet if enough to give us the requested number of bytes by the audio_callback
#ifdef DEBUG_QUEUE
				printf("  AV=1 and Extract from the same packet\n");
#endif
				pkt->size = dimAudioQ;
				memcpy(pkt->data,pkt1->pkt.data+AudioQueueOffset,dimAudioQ);
				pkt->dts = pkt1->pkt.dts;
				pkt->pts = pkt1->pkt.pts;
				pkt->stream_index = pkt1->pkt.stream_index;//1;
				pkt->flags = 1;
				pkt->pos = -1;
				pkt->convergence_duration = -1;
#ifdef DEBUG_QUEUE
				printf("   Adjust timestamps Old = %lld New = %lld\n", pkt1->pkt.dts, (int64_t)(pkt1->pkt.dts + deltaAudioQ + deltaAudioQError));
#endif
				int64_t Olddts=pkt1->pkt.dts;
				pkt1->pkt.dts += deltaAudioQ + deltaAudioQError;
				pkt1->pkt.pts += deltaAudioQ + deltaAudioQError;
				deltaAudioQError=(float)Olddts + deltaAudioQ + deltaAudioQError - (float)pkt1->pkt.dts;
				AudioQueueOffset += dimAudioQ;
#ifdef DEBUG_QUEUE
				printf("   deltaAudioQError = %f\n",deltaAudioQError);
#endif
				//update overall state of queue
				//size is diminished because we played some audio samples
				//but packet is not removed since a portion has still to be played
				//HINT ERRATA we had a size mismatch since size grows with the
				//number of compressed bytes, and diminishes here with the number
				//of raw uncompressed bytes, hence we update size during the
				//real removes and not here anymore
				//q->size -= dimAudioQ;
				update_queue_stats(q, pkt->stream_index);
				//update index of last frame extracted
				q->last_frame_extracted = pkt->stream_index;
#ifdef DEBUG_AUDIO_BUFFER
				printf("1: idx %d    \taqo %d    \tstc %d    \taqe %f    \tpsz %d\n", pkt1->pkt.stream_index, AudioQueueOffset, SizeToCopy, deltaAudioQError, pkt1->pkt.size);
#endif
				ret = 1; //OK
			}
			else {
				//we need bytes from two consecutive packets to satisfy the audio_callback
#ifdef DEBUG_QUEUE
				printf("  AV = 1 and Extract from 2 packets\n");
#endif
				//check for a valid next packet since we will finish the current packet
				//and also take some bytes from the next one
				pkt1->next = seek_and_decode_packet_starting_from(pkt1->next, q);
				if(pkt1->next) {
#ifdef DEBUG_QUEUE
					printf("   we have a next...\n");
#endif
					pkt->size = dimAudioQ;
					pkt->dts = pkt1->pkt.dts;
					pkt->pts = pkt1->pkt.pts;
					pkt->stream_index = pkt1->pkt.stream_index;//1;
					pkt->flags = 1;
					pkt->pos = -1;
					pkt->convergence_duration = -1;
					{
						SizeToCopy=pkt1->pkt.size-AudioQueueOffset;
#ifdef DEBUG_QUEUE
						printf("      SizeToCopy=%d\n",SizeToCopy);
#endif
						memcpy(pkt->data, pkt1->pkt.data+AudioQueueOffset, SizeToCopy);
						memcpy(pkt->data+SizeToCopy, pkt1->next->pkt.data, (dimAudioQ-SizeToCopy)*sizeof(uint8_t));
					}
#ifdef DEBUG_AUDIO_BUFFER
					printf("2: idx %d    \taqo %d    \tstc %d    \taqe %f    \tpsz %d\n", pkt1->pkt.stream_index, AudioQueueOffset, SizeToCopy, deltaAudioQError, pkt1->pkt.size);
#endif
				}
#ifdef DEBUG_AUDIO_BUFFER
				else {
					printf("2: NONEXT\n");
				}
#endif
				//HINT SEE before q->size -= SizeToCopy;
				q->first_pkt = remove_from_queue(q, pkt1);

				// Adjust timestamps
				pkt1 = q->first_pkt;
				if(pkt1) {
					int Offset=(dimAudioQ-SizeToCopy)*1000/(spec.freq*2*spec.channels);
					int64_t LastDts=pkt1->pkt.dts;
					pkt1->pkt.dts += Offset + deltaAudioQError;
					pkt1->pkt.pts += Offset + deltaAudioQError;
					deltaAudioQError = (float)LastDts + (float)Offset + deltaAudioQError - (float)pkt1->pkt.dts;
#ifdef DEBUG_QUEUE
					printf("   Adjust timestamps Old = %lld New = %lld\n", LastDts, pkt1->pkt.dts);
#endif
					AudioQueueOffset = dimAudioQ - SizeToCopy;
					//SEE BEFORE HINT q->size -= AudioQueueOffset;
					ret = 1;
				}
				else {
					AudioQueueOffset=0;
#ifdef DEBUG_AUDIO_BUFFER
					printf("0: idx %d    \taqo %d    \tstc %d    \taqe %f    \tpsz %d\n", pkt1->pkt.stream_index, AudioQueueOffset, SizeToCopy, deltaAudioQError, pkt1->pkt.size);
#endif
				}
#ifdef DEBUG_QUEUE
				printf("   deltaAudioQError = %f\n",deltaAudioQError);
#endif
				update_queue_stats(q, pkt->stream_index);
				//update index of last frame extracted
				q->last_frame_extracted = pkt->stream_index;
			}
		}
	}
	else { //somebody requested a video packet, q is the video queue
		pkt1 = q->first_pkt;
		if(pkt1) {
#ifdef DEBUG_QUEUE
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
			memcpy(pkt->data, pkt1->pkt.data, pkt1->pkt.size);

			//HINT SEE BEFORE q->size -= pkt1->pkt.size;
			q->first_pkt = remove_from_queue(q, pkt1);

			ret = 1;
			update_queue_stats(q, pkt->stream_index);
			//update index of last frame extracted
			q->last_frame_extracted = pkt->stream_index;
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
	printf("QUEUE: Get LastFrameExtracted = %d\n",q->last_frame_extracted);
	printf("QUEUE: Get Tot lost frames = %d\n",q->total_lost_frames);
#endif

	SDL_UnlockMutex(q->mutex);
	return ret;
}


int audio_decode_frame(uint8_t *audio_buf, int buf_size) {
	//struct timeval now;
	int audio_pkt_size = 0;
	long long Now;
	short int DecodeAudio=0, SkipAudio=0;
	//int len1, data_size;

	//gettimeofday(&now,NULL);
	//Now = (now.tv_sec)*1000+now.tv_usec/1000;
	Now=(long long)SDL_GetTicks();

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


	if(audioq.nb_packets>0) {
		if((long long)audioq.first_pkt->pkt.pts+DeltaTime<Now-(long long)MAX_TOLLERANCE) {
			SkipAudio = 1;
			DecodeAudio = 0;
		}
		else if((long long)audioq.first_pkt->pkt.pts+DeltaTime>=Now-(long long)MAX_TOLLERANCE &&
			(long long)audioq.first_pkt->pkt.pts+DeltaTime<=Now+(long long)MAX_TOLLERANCE) {
				SkipAudio = 0;
				DecodeAudio = 1;
		}
	}
		
	while(SkipAudio==1 && audioq.size>0) {
		SkipAudio = 0;
#ifdef DEBUG_AUDIO
 		printf("AUDIO: skipaudio: queue size=%d\n",audioq.size);
#endif
		if(packet_queue_get(&audioq,&AudioPkt,1) < 0) {
			return -1;
		}
		if(audioq.first_pkt)
		{
			if((long long)audioq.first_pkt->pkt.pts+DeltaTime<Now-(long long)MAX_TOLLERANCE) {
				SkipAudio = 1;
				DecodeAudio = 0;
			}
			else if((long long)audioq.first_pkt->pkt.pts+DeltaTime>=Now-(long long)MAX_TOLLERANCE &&
				(long long)audioq.first_pkt->pkt.pts+DeltaTime<=Now+(long long)MAX_TOLLERANCE) {
					SkipAudio = 0;
					DecodeAudio = 1;
			}
		}
	}
	if(DecodeAudio==1) {
		if(packet_queue_get(&audioq,&AudioPkt,1) < 0) {
			return -1;
		}
		memcpy(audio_buf,AudioPkt.data,AudioPkt.size);
		audio_pkt_size = AudioPkt.size;
#ifdef DEBUG_AUDIO
 		printf("AUDIO: Decode audio\n");
#endif
	}

	return audio_pkt_size;
}


int video_callback(void *valthread) {
	//AVPacket pktvideo;
	AVCodecContext  *pCodecCtx;
	AVCodec         *pCodec;
	AVFrame         *pFrame;
	AVPacket        packet;
	int frameFinished;
	int countexit;
	AVPicture pict;
	//FILE *frecon;
	SDL_Event event;
	long long Now;
	short int SkipVideo, DecodeVideo;

	//double frame_rate = 0.0,time_between_frames=0.0;
	//struct timeval now;

	//int wait_for_sync = 1;
	ThreadVal *tval;
	tval = (ThreadVal *)valthread;

	//frame_rate = tval->framerate;
	//time_between_frames = 1.e6 / frame_rate;
	//gettimeofday(&time_now,0);

	//frecon = fopen("recondechunk.mpg","wb");

	pCodecCtx=avcodec_alloc_context();
	pCodecCtx->codec_type = CODEC_TYPE_VIDEO;
#ifdef H264_VIDEO_ENCODER
	pCodecCtx->codec_id  = CODEC_ID_H264;
	pCodecCtx->me_range = 16;
	pCodecCtx->max_qdiff = 4;
	pCodecCtx->qmin = 10;
	pCodecCtx->qmax = 51;
	pCodecCtx->qcompress = 0.6;
#else
	pCodecCtx->codec_id  = CODEC_ID_MPEG4;
#endif
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

	while(!quit) {
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
			if(((long long)videoq.first_pkt->pkt.pts+DeltaTime)<Now-(long long)MAX_TOLLERANCE) {
				SkipVideo = 1;
				DecodeVideo = 0;
			}
			else 
				if(((long long)videoq.first_pkt->pkt.pts+DeltaTime)>=Now-(long long)MAX_TOLLERANCE &&
				   ((long long)videoq.first_pkt->pkt.pts+DeltaTime)<=Now+(long long)MAX_TOLLERANCE) {
					SkipVideo = 0;
					DecodeVideo = 1;
				}
		}
#ifdef DEBUG_VIDEO
		printf("VIDEO: skipvideo:%d decodevideo:%d\n",SkipVideo,DecodeVideo);
#endif

		while(SkipVideo==1 && videoq.size>0) {
			SkipVideo = 0;
#ifdef DEBUG_VIDEO 
 			printf("VIDEO: Skip Video\n");
#endif
			if(packet_queue_get(&videoq,&VideoPkt,0) < 0) {
				break;
			}
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &VideoPkt);
			if(videoq.first_pkt)
			{
				if((long long)videoq.first_pkt->pkt.pts+DeltaTime<Now-(long long)MAX_TOLLERANCE) {
					SkipVideo = 1;
					DecodeVideo = 0;
				}
				else if((long long)videoq.first_pkt->pkt.pts+DeltaTime>=Now-(long long)MAX_TOLLERANCE &&
								(long long)videoq.first_pkt->pkt.pts+DeltaTime<=Now+(long long)MAX_TOLLERANCE) {
					SkipVideo = 0;
					DecodeVideo = 1;
				}
			}
		}
		
		if(DecodeVideo==1) {
			if(packet_queue_get(&videoq,&VideoPkt,0) > 0) {

#ifdef DEBUG_VIDEO
				printf("VIDEO: Decode video FrameTime=%lld Now=%lld\n",(long long)VideoPkt.pts+DeltaTime,Now);
#endif

				avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &VideoPkt);

				if(frameFinished) { // it must be true all the time else error
#ifdef DEBUG_VIDEO
					printf("VIDEO: FrameFinished\n");
#endif
					if(SaveYUV)
						SaveFrame(pFrame, pCodecCtx->width, pCodecCtx->height);
					//fwrite(pktvideo.data, 1, pktvideo.size, frecon);

					if(silentMode)
						continue;

					// Lock SDL_yuv_overlay
					if(SDL_MUSTLOCK(screen)) {
						if(SDL_LockSurface(screen) < 0) {
							continue;
						}
					}

					if(SDL_LockYUVOverlay(yuv_overlay) < 0) {
						if(SDL_MUSTLOCK(screen)) {
							SDL_UnlockSurface(screen);
						}
						continue;
					}
					pict.data[0] = yuv_overlay->pixels[0];
					pict.data[1] = yuv_overlay->pixels[2];
					pict.data[2] = yuv_overlay->pixels[1];

					pict.linesize[0] = yuv_overlay->pitches[0];
					pict.linesize[1] = yuv_overlay->pitches[2];
					pict.linesize[2] = yuv_overlay->pitches[1];

					if(img_convert_ctx == NULL) {
						img_convert_ctx = sws_getContext(tval->width, tval->height, PIX_FMT_YUV420P, initRect->w, initRect->h, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
						if(img_convert_ctx == NULL) {
							fprintf(stderr, "Cannot initialize the conversion context!\n");
							exit(1);
						}
					}
					// let's draw the data (*yuv[3]) on a SDL screen (*screen)
					sws_scale(img_convert_ctx, pFrame->data, pFrame->linesize, 0, tval->height, pict.data, pict.linesize);
					SDL_UnlockYUVOverlay(yuv_overlay);
					// Show, baby, show!
					SDL_DisplayYUVOverlay(yuv_overlay, &rect);

					//redisplay logo
					/**SDL_BlitSurface(image, NULL, screen, &dest);*/
					/* Update the screen area just changed */
					/**SDL_UpdateRects(screen, 1, &dest);*/
					
					refresh_fullscreen_button(fullscreenButtonHover);

					if(SDL_MUSTLOCK(screen)) {
						SDL_UnlockSurface(screen);
					}
				} //if FrameFinished
			} // if packet_queue_get
		} //if DecodeVideo=1

		usleep(5000);
	}
	av_free(pCodecCtx);
	//fclose(frecon);
#ifdef DEBUG_VIDEO
 	printf("VIDEO: video callback end\n");
#endif
	return 1;
}


void aspect_ratio_rect(float aspect_ratio, int width, int height)
{
	int h = 0, w = 0, x, y;
	aspect_ratio_resize(aspect_ratio, width, height, &w, &h);
	x = (width - w) / 2;
	y = (height - h) / 2;
	rect.x = x;//x;
	rect.y = y;//y;
	rect.w = w;
	rect.h = h;

// 	printf("setting video mode %dx%d\n", rect.w, rect.h);
}


void audio_callback(void *userdata, Uint8 *stream, int len) {

	//AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
	int audio_size;

	static uint8_t audio_buf[AVCODEC_MAX_AUDIO_FRAME_SIZE];

	audio_size = audio_decode_frame(audio_buf, sizeof(audio_buf));
	if(audio_size != len) {
		memset(stream, 0, len);
	} else {
		memcpy(stream, (uint8_t *)audio_buf, len);
	}
}

void SetupGUI()
{
	// init SDL_image
	int flags=IMG_INIT_JPG|IMG_INIT_PNG;
	int initted=IMG_Init(flags);
	if(initted&flags != flags) {
		printf("IMG_Init: Failed to init required jpg and png support!\n");
		printf("IMG_Init: %s\n", IMG_GetError());
		exit(1);
	}

	SDL_Surface *temp;
	int screen_w = 0, screen_h = 0;

	/* Load a BMP file on a surface */
	temp = IMG_Load(FULLSCREEN_ICON_FILE);
	if (temp == NULL) {
		fprintf(stderr, "Error loading %s: %s\n", FULLSCREEN_ICON_FILE, SDL_GetError());
		exit(1);
	}

	if(rect.w > temp->w)
		screen_w = rect.w;
	else
		screen_w = temp->w;

		screen_h = rect.h + temp->h + BUTTONS_LAYER_OFFSET;

	SDL_WM_SetCaption("Filling buffer...", NULL);
	// Make a screen to put our video
#ifndef __DARWIN__
	screen = SDL_SetVideoMode(screen_w, screen_h, 0, SDL_SWSURFACE | SDL_RESIZABLE);
#else
	screen = SDL_SetVideoMode(screen_w, screen_h, 24, SDL_SWSURFACE | SDL_RESIZABLE);
#endif
	if(!screen) {
		fprintf(stderr, "SDL_SetVideoMode returned null: could not set video mode - exiting\n");
		exit(1);
	}
	
	window_width = screen_w;
	window_height = screen_h;
	
	fullscreenIcon = SDL_DisplayFormatAlpha(temp);
	SDL_FreeSurface(temp);

	// load icon buttons
	temp = IMG_Load(NOFULLSCREEN_ICON_FILE);
	if (temp == NULL) {
		fprintf(stderr, "Error loading %s: %s\n", NOFULLSCREEN_ICON_FILE, SDL_GetError());
		exit(1);
	}
	nofullscreenIcon = SDL_DisplayFormatAlpha(temp);
	SDL_FreeSurface(temp);

	temp = IMG_Load(NOFULLSCREEN_HOVER_ICON_FILE);
	if (temp == NULL) {
		fprintf(stderr, "Error loading %s: %s\n", NOFULLSCREEN_HOVER_ICON_FILE, SDL_GetError());
		exit(1);
	}
	nofullscreenHoverIcon = SDL_DisplayFormatAlpha(temp);
	SDL_FreeSurface(temp);

	temp = IMG_Load(FULLSCREEN_HOVER_ICON_FILE);
	if (temp == NULL) {
		fprintf(stderr, "Error loading %s: %s\n", FULLSCREEN_HOVER_ICON_FILE, SDL_GetError());
		exit(1);
	}
	fullscreenHoverIcon = SDL_DisplayFormatAlpha(temp);
	SDL_FreeSurface(temp);

	defaultCursor = SDL_GetCursor();
	handCursor = init_system_cursor(handXPM);

	/* Copy on the screen surface 
	surface should be blocked now.
	*/
	fullscreenIconBox.x = (screen_w - fullscreenIcon->w) / 2;
	fullscreenIconBox.y = rect.h + (BUTTONS_LAYER_OFFSET/2);
	fullscreenIconBox.w = fullscreenIcon->w;
	fullscreenIconBox.h = fullscreenIcon->h;
	printf("x%d y%d w%d h%d\n", fullscreenIconBox.x, fullscreenIconBox.y, fullscreenIconBox.w, fullscreenIconBox.h);

	//create video overlay for display of video frames
	yuv_overlay = SDL_CreateYUVOverlay(rect.w, rect.h, SDL_YV12_OVERLAY, screen);

	if ( yuv_overlay == NULL ) {
		fprintf(stderr,"SDL: Couldn't create SDL_yuv_overlay: %s", SDL_GetError());
		exit(1);
	}

	if ( yuv_overlay->hw_overlay )
		fprintf(stderr,"SDL: Using hardware overlay.");
	rect.x = (screen_w - rect.w) / 2;
	SDL_DisplayYUVOverlay(yuv_overlay, &rect);

	/**SDL_BlitSurface(image, NULL, screen, &dest);*/
	SDL_BlitSurface(fullscreenIcon, NULL, screen, &fullscreenIconBox);
	/* Update the screen area just changed */
	SDL_UpdateRects(screen, 1, &fullscreenIconBox);
}

void SaveFrame(AVFrame *pFrame, int width, int height) {
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

void sigint_handler (int signal) {
	printf("Caught SIGINT, exiting...");
	got_sigint = 1;
}

void ProcessKeys() {
	static Uint32 LastTime=0;
	static int LastKey=-1;

	Uint32 Now=SDL_GetTicks();
	Uint8* keystate=SDL_GetKeyState(NULL);
	if(keystate[SDLK_SPACE] &&
	  (LastKey!=SDLK_SPACE || (LastKey==SDLK_SPACE && (Now-LastTime>1000))))
	{
		LastKey=SDLK_SPACE;
		LastTime=Now;
		QueueStopped=!QueueStopped;
		if(QueueStopped) CurrStatus = PAUSED;
		else CurrStatus = RUNNING;
		refresh_fullscreen_button(0);
	}
	if(keystate[SDLK_ESCAPE] &&
	  (LastKey!=SDLK_ESCAPE || (LastKey==SDLK_ESCAPE && (Now-LastTime>1000))))
	{
		LastKey=SDLK_ESCAPE;
		LastTime=Now;
		quit=1;
	}
	/*if(keystate[SDLK_f] &&
	  (LastKey!=SDLK_f || (LastKey==SDLK_f && (Now-LastTime>1000))))
	{
		LastKey=SDLK_f;
		LastTime=Now;
		SDL_WM_ToggleFullScreen(NULL);
	}*/
}

int main(int argc, char *argv[]) {
	int i, j, videoStream, outbuf_size, out_size, out_size_audio, seq_current_chunk = 0, audioStream;
	int len1, data_size, stime, cont=0;
	int frameFinished, len_audio;
	int numBytes, outbuf_audio_size, audio_size;

	int y;
	
	uint8_t *outbuf,*outbuf_audio;
	uint8_t *outbuf_audi_audio;
	int httpPort = -1;
	
	AVFormatContext *pFormatCtx;

	AVCodec         *pCodec,*aCodec;
	AVFrame         *pFrame; 

	AVPicture pict;
	SDL_Thread *video_thread;//exit_thread,*exit_thread2;
	SDL_Event event;
	SDL_AudioSpec wanted_spec;
	
	struct MHD_Daemon *daemon = NULL;	

	char buf[1024],outfile[1024], basereadfile[1024],readfile[1024];
	FILE *fp;	
	int width,height,asample_rate,achannels;

	ThreadVal *tval;
	tval = (ThreadVal *)malloc(sizeof(ThreadVal));
		
	if(argc<9) {
		printf("chunker_player width height aspect_ratio audio_sample_rate audio_channels queue_thresh httpd_port silentMode <YUVFilename>\n");
		exit(1);
	}
	sscanf(argv[1],"%d",&width);
	sscanf(argv[2],"%d",&height);
	sscanf(argv[3],"%f",&ratio);
	sscanf(argv[4],"%d",&asample_rate);
	sscanf(argv[5],"%d",&achannels);
	sscanf(argv[6],"%d",&queue_filling_threshold);
	sscanf(argv[7],"%d",&httpPort);
	sscanf(argv[8],"%d",&silentMode);
	
	if(argc==10)
	{
		sscanf(argv[9],"%s",YUVFileName);
		printf("YUVFile: %s\n",YUVFileName);
		FILE* fp=fopen(YUVFileName, "wb");
		if(fp)
		{
			SaveYUV=1;
			fclose(fp);
		}
		else
			printf("ERROR: Unable to create YUVFile\n");
	}

	tval->width = width;
	tval->height = height;
	tval->aspect_ratio = ratio;

	// Register all formats and codecs
	av_register_all();
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}

	aCodecCtx = avcodec_alloc_context();
	//aCodecCtx->bit_rate = 64000;
	aCodecCtx->sample_rate = asample_rate;
	aCodecCtx->channels = achannels;
#ifdef MP3_AUDIO_ENCODER
	aCodec = avcodec_find_decoder(CODEC_ID_MP3); // codec audio
#else
	aCodec = avcodec_find_decoder(CODEC_ID_MP2);
#endif
	printf("MP2 codec id %d MP3 codec id %d\n",CODEC_ID_MP2,CODEC_ID_MP3);
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
	wanted_spec.freq = aCodecCtx->sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = aCodecCtx->channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
	wanted_spec.callback = audio_callback;
	wanted_spec.userdata = aCodecCtx;
	if(SDL_OpenAudio(&wanted_spec,&spec)<0) {
		fprintf(stderr,"SDL_OpenAudio: %s\n",SDL_GetError());
		return -1;
	}
	dimAudioQ = spec.size;
	deltaAudioQ = (float)((float)spec.samples)*1000/spec.freq;

#ifdef DEBUG_AUDIO
	printf("freq:%d\n",spec.freq);
	printf("format:%d\n",spec.format);
	printf("channels:%d\n",spec.channels);
	printf("silence:%d\n",spec.silence);
	printf("samples:%d\n",spec.samples);
	printf("size:%d\n",spec.size);
	printf("deltaAudioQ: %f\n",deltaAudioQ);
#endif

	pFrame=avcodec_alloc_frame();
	if(pFrame==NULL) {
		printf("Memory error!!!\n");
		return -1;
	}
	outbuf_audio = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);

	//initialize the audio and the video queues
	packet_queue_init(&audioq, AUDIO);
	packet_queue_init(&videoq, VIDEO);

	//calculate aspect ratio and put updated values in rect
	aspect_ratio_rect(ratio, width, height);

	initRect = (SDL_Rect*) malloc(sizeof(SDL_Rect));
	if(!initRect)
	{
		printf("Memory error!!!\n");
		return -1;
	}
	initRect->x = rect.x;
	initRect->y = rect.y;
	initRect->w = rect.w;
	initRect->h = rect.h;

	//SetupGUI("napalogo_small.bmp");
	if(!silentMode)
		SetupGUI();
	
	// Init audio and video buffers
	av_init_packet(&AudioPkt);
	av_init_packet(&VideoPkt);
	AudioPkt.data=(uint8_t *)malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
	if(!AudioPkt.data) return 0;
	VideoPkt.data=(uint8_t *)malloc(width*height*3/2);
	if(!VideoPkt.data) return 0;
	
	SDL_PauseAudio(0);
	video_thread = SDL_CreateThread(video_callback,tval);
	
	//this thread fetches chunks from the network by listening to the following path, port
	daemon = initChunkPuller(UL_DEFAULT_EXTERNALPLAYER_PATH, httpPort);
	CurrStatus = RUNNING;

	// Wait for user input
	while(!quit) {
		if(QueueFillingMode) {
			SDL_WM_SetCaption("Filling buffer...", NULL);

			if(audioq.nb_packets==0 && audioq.last_frame_extracted>0) {	// video ended therefore init queues
#ifdef DEBUG_QUEUE
				printf("QUEUE: MAIN SHOULD RESET\n");
#endif
				packet_queue_reset(&audioq, AUDIO);
				packet_queue_reset(&videoq, VIDEO);
			}

#ifdef DEBUG_QUEUE
			//printf("QUEUE: MAIN audio:%d video:%d audiolastframe:%d videolastframe:%d\n", audioq.nb_packets, videoq.nb_packets, audioq.last_frame_extracted, videoq.last_frame_extracted);
#endif
		}
		else
			SDL_WM_SetCaption("NAPA-Wine Player", NULL);

		int x = 0, y = 0;
		int resize_w, resize_h;
		int tmp_switch = 0;
		//listen for key and mouse
		while(SDL_PollEvent(&event)) {
			switch(event.type) {
				case SDL_QUIT:
					//exit(0);
					quit=1;
				break;
				case SDL_VIDEORESIZE:
					//printf("\tSDL_VIDEORESIZE\n");
					
					// if running, pause until resize has done
					if(CurrStatus == RUNNING)
					{
						QueueStopped = 1;
						CurrStatus = PAUSED;
						tmp_switch = 1;
					}
#ifndef __DARWIN__
					screen = SDL_SetVideoMode(event.resize.w, event.resize.h, 0, SDL_SWSURFACE | SDL_RESIZABLE);
#else
					screen = SDL_SetVideoMode(event.resize.w, event.resize.h, 24, SDL_SWSURFACE | SDL_RESIZABLE);
#endif
					if(!screen) {
						fprintf(stderr, "SDL_SetVideoMode returned null: could not set video mode - exiting\n");
						exit(1);
					}
					
					window_width = event.resize.w;
					window_height = event.resize.h;
					
					aspect_ratio_rect(ratio, event.resize.w, event.resize.h - BUTTONS_LAYER_OFFSET - fullscreenIconBox.h);
					
					fullscreenIconBox.x = (event.resize.w - fullscreenIcon->w) / 2;

					// put the button just below the video overlay
// 					fullscreenIcon.y = rect.y+rect.h + (BUTTONS_LAYER_OFFSET/2);

					// put the button at the bottom edge of the screen
					fullscreenIconBox.y = event.resize.h - fullscreenIconBox.h - (BUTTONS_LAYER_OFFSET/2);
					
					// refresh_fullscreen_button(0);
					
					if(tmp_switch)
					{
						QueueStopped = 0;
						CurrStatus= RUNNING;
						tmp_switch = 0;
					}
				break;
				case SDL_ACTIVEEVENT:
					//printf("\tSDL_ACTIVEEVENT\n");
					// if the window was iconified or restored
					/*if(event.active.state & SDL_APPACTIVE)
					{
						//If the application is being reactivated
						if( event.active.gain != 0 )
						{
							//SDL_WM_SetCaption( "Window Event Test restored", NULL );
						}
					}

					//If something happened to the keyboard focus
					else if( event.active.state & SDL_APPINPUTFOCUS )
					{
						//If the application gained keyboard focus
						if( event.active.gain != 0 )
						{
						}
					}
					//If something happened to the mouse focus
					else if( event.active.state & SDL_APPMOUSEFOCUS )
					{
						//If the application gained mouse focus
						if( event.active.gain != 0 )
						{
						}
					}*/
					break;
				case SDL_MOUSEMOTION:
					//printf("\tSDL_MOUSEMOTION\n");
					x = event.motion.x;
					y = event.motion.y;
					//If the mouse is over the button
					if(
						( x > fullscreenIconBox.x ) && ( x < fullscreenIconBox.x + fullscreenIcon->w )
						&& ( y > fullscreenIconBox.y ) && ( y < fullscreenIconBox.y + fullscreenIcon->h )
					)
					{
						fullscreenButtonHover = 1;
						SDL_SetCursor(handCursor);
					}
					//If not
					else
					{
						fullscreenButtonHover = 0;
						SDL_SetCursor(defaultCursor);
					}
				break;
				case SDL_MOUSEBUTTONUP:
					//printf("\tSDL_MOUSEBUTTONUP\n");
					if( event.button.button != SDL_BUTTON_LEFT )
        					break;
					
					x = event.motion.x;
					y = event.motion.y;
					//If the mouse is over the button
					if(
						( x > fullscreenIconBox.x ) && ( x < fullscreenIconBox.x + fullscreenIcon->w )
						&& ( y > fullscreenIconBox.y ) && ( y < fullscreenIconBox.y + fullscreenIcon->h )
					)
					{
						
						// if running, pause until resize has done
						if(CurrStatus == RUNNING)
						{
							QueueStopped = 1;
							CurrStatus = PAUSED;
							tmp_switch = 1;
						}
						
						toggle_fullscreen();
						
						fullscreenButtonHover = 1;
						
						if(tmp_switch)
						{
							QueueStopped = 0;
							CurrStatus= RUNNING;
							tmp_switch = 0;
						}
					}
				break;
			}
			ProcessKeys();
		}
		usleep(120000);
	}

	//TERMINATE
	IMG_Quit();

	// Stop audio&video playback
	SDL_WaitThread(video_thread,NULL);
	SDL_PauseAudio(1);
	SDL_CloseAudio();
	//SDL_DestroyMutex(timing_mutex);
	SDL_Quit();
	av_free(aCodecCtx);
	free(AudioPkt.data);
	free(VideoPkt.data);
	free(outbuf_audio);
	finalizeChunkPuller(daemon);
	free(tval);
	free(initRect);
	return 0;
}

int enqueueBlock(const uint8_t *block, const int block_size) {
	Chunk *gchunk = NULL;
	ExternalChunk *echunk = NULL;
	int decoded_size = -1;
	uint8_t *tempdata, *buffer;
	int i, j;
	Frame *frame = NULL;
	AVPacket packet, packetaudio;

	uint16_t *audio_bufQ = NULL;
	int16_t *dataQ = NULL;
	int data_sizeQ;
	int lenQ;

	//the frame.h gets encoded into 5 slots of 32bits (3 ints plus 2 more for the timeval struct
	static int sizeFrameHeader = 5*sizeof(int32_t);
	static int ExternalChunk_header_size = 5*CHUNK_TRANSCODING_INT_SIZE + 2*CHUNK_TRANSCODING_INT_SIZE + 2*CHUNK_TRANSCODING_INT_SIZE + 1*CHUNK_TRANSCODING_INT_SIZE*2;

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
#ifdef DEBUG_CHUNKER
	printf("CHUNKER: enqueueBlock: decoded_size %d target size %d\n", decoded_size, GRAPES_ENCODED_CHUNK_HEADER_SIZE + ExternalChunk_header_size + gchunk->size);
#endif
  if(decoded_size < 0 || decoded_size != GRAPES_ENCODED_CHUNK_HEADER_SIZE + ExternalChunk_header_size + gchunk->size) {
		//HINT here i should differentiate between various return values of the decode
		//in order to free what has been allocated there
		printf("chunk probably corrupted!\n");
		av_free(audio_bufQ);
		free(gchunk);
		return PLAYER_FAIL_RETURN;
	}

	echunk = grapesChunkToExternalChunk(gchunk);
	if(echunk == NULL) {
		printf("Memory error in echunk!\n");
		free(gchunk->attributes);
		free(gchunk->data);
		free(gchunk);
		return PLAYER_FAIL_RETURN;
	}
	free(gchunk->attributes);
	free(gchunk);

	frame = (Frame *)malloc(sizeof(Frame));
	if(!frame) {
		printf("Memory error in Frame!\n");
		if(gchunk->attributes)
			free(gchunk->attributes);
		if(echunk->data)
			free(echunk->data);
		if(echunk)
			free(echunk);
		av_free(audio_bufQ);
		return PLAYER_FAIL_RETURN;
	}

	tempdata = echunk->data; //let it point to first frame of payload
	j=echunk->payload_len;
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
			if(packet.size > 0)
				packet_queue_put(&videoq, &packet); //the _put makes a copy of the packet

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
			if(packetaudio.size > 0)
				packet_queue_put(&audioq, &packetaudio);//makes a copy of the packet so i can free here

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
	if(echunk->data)
		free(echunk->data);
	if(echunk)
		free(echunk);
	if(frame)
		free(frame);
	if(audio_bufQ)
		av_free(audio_bufQ);
}

/* From SDL documentation. */
SDL_Cursor *init_system_cursor(const char *image[])
{
	int i, row, col;
	Uint8 data[4*32];
	Uint8 mask[4*32];
	int hot_x, hot_y;

	i = -1;
	for ( row=0; row<32; ++row ) {
		for ( col=0; col<32; ++col ) {
			if ( col % 8 ) {
				data[i] <<= 1;
				mask[i] <<= 1;
			} else {
				++i;
				data[i] = mask[i] = 0;
			}
			
			switch (image[4+row][col]) {
				case ' ':
					data[i] |= 0x01;
					mask[i] |= 0x01;
					break;
				case '.':
					mask[i] |= 0x01;
					break;
				case 'X':
					break;
			}
		}
	}
	
	sscanf(image[4+row], "%d,%d", &hot_x, &hot_y);
	return SDL_CreateCursor(data, mask, 32, 32, hot_x, hot_y);
}

void refresh_fullscreen_button(int hover)
{
	if(!hover)
	{
		if(!fullscreen)
		{
			SDL_BlitSurface(fullscreenIcon, NULL, screen, &fullscreenIconBox);
			SDL_UpdateRects(screen, 1, &fullscreenIconBox);
		}
		else
		{
			SDL_BlitSurface(nofullscreenIcon, NULL, screen, &fullscreenIconBox);
			SDL_UpdateRects(screen, 1, &fullscreenIconBox);
		}
	}
	else
	{
		if(!fullscreen)
		{
			SDL_BlitSurface(fullscreenHoverIcon, NULL, screen, &fullscreenIconBox);
			SDL_UpdateRects(screen, 1, &fullscreenIconBox);
		}
		else
		{
			SDL_BlitSurface(nofullscreenHoverIcon, NULL, screen, &fullscreenIconBox);
			SDL_UpdateRects(screen, 1, &fullscreenIconBox);
		}
	}
}

void aspect_ratio_resize(float aspect_ratio, int width, int height, int* out_width, int* out_height)
{
	int h,w,x,y;
	h = (int)((float)width/aspect_ratio);
	if(h<=height)
	{
		w = width;
	}
	else
	{
		w = (int)((float)height*aspect_ratio);
		h = height;
	}
	*out_width = w;
	*out_height = h;
}

void toggle_fullscreen()
{
	//If the screen is windowed
	if( !fullscreen )
	{
		//Set the screen to fullscreen
#ifndef __DARWIN__
		// screen = SDL_SetVideoMode(FULLSCREEN_WIDTH, FULLSCREEN_HEIGHT, 0, SDL_SWSURFACE | SDL_RESIZABLE | SDL_FULLSCREEN);
		screen = SDL_SetVideoMode(FULLSCREEN_WIDTH, FULLSCREEN_HEIGHT, 0, SDL_SWSURFACE | SDL_NOFRAME | SDL_FULLSCREEN);
#else
		// screen = SDL_SetVideoMode(FULLSCREEN_WIDTH, FULLSCREEN_HEIGHT, 24, SDL_SWSURFACE | SDL_RESIZABLE | SDL_FULLSCREEN);
		screen = SDL_SetVideoMode(FULLSCREEN_WIDTH, FULLSCREEN_HEIGHT, 24, SDL_SWSURFACE | SDL_NOFRAME | SDL_FULLSCREEN);
#endif

		//If there's an error
		if( screen == NULL )
		{
			fprintf(stderr, "SDL_SetVideoMode returned null: could not toggle fullscreen mode - exiting\n");
			exit(1);
		}
		
		aspect_ratio_rect(ratio, FULLSCREEN_WIDTH, FULLSCREEN_HEIGHT - BUTTONS_LAYER_OFFSET - fullscreenIconBox.h);
		fullscreenIconBox.x = (FULLSCREEN_WIDTH - fullscreenIcon->w) / 2;
		// fullscreenIcon.y = rect.y+rect.h + (BUTTONS_LAYER_OFFSET/2); // put the button just below the video overlay
		fullscreenIconBox.y = FULLSCREEN_HEIGHT - fullscreenIconBox.h - (BUTTONS_LAYER_OFFSET/2); // put the button at the bottom edge of the screen
		
		//Set the window state flag
		fullscreen = 1;
	}
	
	//If the screen is fullscreen
	else
	{
		//Window the screen
#ifndef __DARWIN__
		screen = SDL_SetVideoMode(window_width, window_height, 0, SDL_SWSURFACE | SDL_RESIZABLE);
#else
		screen = SDL_SetVideoMode(window_width, window_height, 24, SDL_SWSURFACE | SDL_RESIZABLE);
#endif
		
		//If there's an error
		if( screen == NULL )
		{
			fprintf(stderr, "SDL_SetVideoMode returned null: could not toggle fullscreen mode - exiting\n");
			exit(1);
		}
		
		aspect_ratio_rect(ratio, window_width, window_height - BUTTONS_LAYER_OFFSET - fullscreenIconBox.h);
		fullscreenIconBox.x = (window_width - fullscreenIcon->w) / 2;
		// fullscreenIcon.y = rect.y+rect.h + (BUTTONS_LAYER_OFFSET/2); // put the button just below the video overlay
		fullscreenIconBox.y = window_height - fullscreenIconBox.h - (BUTTONS_LAYER_OFFSET/2); // put the button at the bottom edge of the screen
		
		//Set the window state flag
		fullscreen = 0;
	}
}
