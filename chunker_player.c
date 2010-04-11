// player.c
// Author 
// Diego Reforgiato, Dario Marchese, Carmelo Daniele
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

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#include <platform.h>
#include <microhttpd.h>
//#include <AntTweakBar.h>

#include "chunker_player.h"
#include "codec_definitions.h"

#define SDL_AUDIO_BUFFER_SIZE 1024

#define QUEUE_FILLING_THRESHOLD	50
#define AUDIO	1
#define VIDEO	2

#define DEBUG_AUDIO
#define DEBUG_VIDEO
#define DEBUG_QUEUE
#define DEBUG_SOURCE

short int QueueFillingMode=1;
short int QueueStopped=0;

typedef struct PacketQueue {
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;
	short int queueType;
	int last_frame_extracted;
	int total_lost_frames;
} PacketQueue;

typedef struct threadVal {
	int width;
	int height;
} ThreadVal;

int AudioQueueOffset=0;
PacketQueue audioq;
PacketQueue videoq;
AVPacket AudioPkt, VideoPkt;
int quit = 0;

SDL_Surface *screen;
SDL_Overlay *yuv_overlay;
SDL_Rect    rect;

int got_sigint = 0;

#define MAX_TOLLERANCE 60

long long DeltaTime;
short int FirstTimeAudio=1, FirstTimeVideo = 1, FirstTime = 1;

int dimAudioQ;
float deltaAudioQ;

void packet_queue_init(PacketQueue *q, short int Type) {
	memset(q,0,sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
	QueueFillingMode=1;
	q->queueType=Type;
	q->last_frame_extracted = 0;
	q->total_lost_frames = 0;
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
	short int skip = 0;
	AVPacketList *pkt1, *tmp, *prevtmp;
	if(av_dup_packet(pkt) < 0) {
		return -1;
	}
	pkt1 = av_malloc(sizeof(AVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;

	SDL_LockMutex(q->mutex);

// INSERTION SORT ALGORITHM
// before inserting pkt, check if pkt.stream_index is <= current_extracted_frame.

	if(pkt->stream_index>q->last_frame_extracted) {

// either checking starting from the first_pkt or needed other struct like AVPacketList with next and prev....
		if (!q->last_pkt)
			q->first_pkt = pkt1;
		else {
			tmp = q->first_pkt;
			while(tmp->pkt.stream_index<pkt->stream_index) {
				prevtmp = tmp;
				tmp = tmp->next;
				if(!tmp)
					break;
			}
			if(tmp && tmp->pkt.stream_index==pkt->stream_index) {
				skip = 1;
			}
			else {
				prevtmp->next = pkt1;
				pkt1->next = tmp;
			}
//			q->last_pkt->next = pkt1; // It was uncommented when not insertion sort
		}
		if(skip==0) {
			q->last_pkt = pkt1;
			q->nb_packets++;
			q->size += pkt1->pkt.size;
#ifdef DEBUG_QUEUE
			printf("PUT in Queue: NPackets=%d Type=%d\n",q->nb_packets,q->queueType);
#endif

			if(q->nb_packets>=QUEUE_FILLING_THRESHOLD && QueueFillingMode) // && q->queueType==AUDIO)
			{
				QueueFillingMode=0;
#ifdef DEBUG_QUEUE
			printf("PUT in Queue: FillingMode set to zero\n");
#endif
				//SDL_CondSignal(q->cond);
			}
//WHYYYYYYYYY			q->last_frame_extracted = pkt->stream_index;
//#ifdef DEBUG_QUEUE
//			printf("PUT LastFrameExtracted set to %d\n",q->last_frame_extracted);
//#endif
		}
	}

	SDL_UnlockMutex(q->mutex);
	return 0;
}

static int packet_queue_get(PacketQueue *q, AVPacket *pkt, short int av) {
	//AVPacket tmp;
	AVPacketList *pkt1;
	int ret=-1;
	int SizeToCopy=0;

#ifdef DEBUG_QUEUE
	printf("QUEUE Get NPackets=%d Type=%d\n",q->nb_packets,q->queueType);
#endif

	if((q->queueType==AUDIO && QueueFillingMode) || QueueStopped)
	{
		return -1;
		//SDL_CondWait(q->cond, q->mutex);
	}

	SDL_LockMutex(q->mutex);
	pkt1 = q->first_pkt;
	if (pkt1) {
		if(av==1) {
			if(pkt1->pkt.size-AudioQueueOffset>dimAudioQ) {
#ifdef DEBUG_QUEUE
				printf("  AV=1 and Extract from the same packet\n");
#endif
				//av_init_packet(&tmp);
				q->size -= dimAudioQ;
				pkt->size = dimAudioQ;
				//tmp.data = pkt1->pkt.data+AudioQueueOffset;
				memcpy(pkt->data,pkt1->pkt.data+AudioQueueOffset,dimAudioQ);
				pkt->dts = pkt1->pkt.dts;
				pkt->pts = pkt1->pkt.pts;
				pkt->stream_index = pkt1->pkt.stream_index;//1;
				pkt->flags = 1;
				pkt->pos = -1;
				pkt->convergence_duration = -1;

				//*pkt = tmp;
				//pkt1->pkt.size -= dimAudioQ;
				pkt1->pkt.dts += deltaAudioQ;
				pkt1->pkt.pts += deltaAudioQ;
				AudioQueueOffset += dimAudioQ;
#ifdef DEBUG_QUEUE
				printf("   AudioQueueOffset = %d\n",AudioQueueOffset);
#endif
				
				ret = 1;
				//compute lost frame statistics
			if(q->last_frame_extracted > 0 && pkt->stream_index > q->last_frame_extracted) {
#ifdef DEBUG_QUEUE
					printf("  stats: stidx %d last %d tot %d\n", pkt->stream_index, q->last_frame_extracted, q->total_lost_frames);
#endif
					q->total_lost_frames = q->total_lost_frames + pkt->stream_index - q->last_frame_extracted - 1;
				}
				//update index of last frame extracted
				q->last_frame_extracted = pkt->stream_index;
			}
			else {
#ifdef DEBUG_QUEUE
				printf("  AV = 1 and Extract from 2 packets\n");
#endif
				// Check for loss
				if(pkt1->next)
				{
#ifdef DEBUG_QUEUE
					printf("   we have a next...\n");
#endif
					//av_init_packet(&tmp);
					pkt->size = dimAudioQ;
					pkt->dts = pkt1->pkt.dts;
					pkt->pts = pkt1->pkt.pts;
					pkt->stream_index = pkt1->pkt.stream_index;//1;
					pkt->flags = 1;
					pkt->pos = -1;
					pkt->convergence_duration = -1;
					//tmp.data = (uint8_t *)malloc(sizeof(uint8_t)*dimAudioQ);
					//if(tmp.data)
					{
						SizeToCopy=pkt1->pkt.size-AudioQueueOffset;
#ifdef DEBUG_QUEUE
						printf("      SizeToCopy=%d\n",SizeToCopy);
#endif
						memcpy(pkt->data,pkt1->pkt.data+AudioQueueOffset,SizeToCopy);
						memcpy(pkt->data+SizeToCopy,pkt1->next->pkt.data,(dimAudioQ-SizeToCopy)*sizeof(uint8_t));
					}
					//*pkt = tmp;
				}
				q->first_pkt = pkt1->next;
				if (!q->first_pkt)
					q->last_pkt = NULL;
				q->nb_packets--;
				q->size -= SizeToCopy;
#ifdef DEBUG_QUEUE
				printf("  about to free... ");
#endif
				free(pkt1->pkt.data);
				av_free(pkt1);
#ifdef DEBUG_QUEUE
				printf("freeed\n");
#endif
				// Adjust timestamps
				pkt1 = q->first_pkt;
				if(pkt1)
				{
					pkt1->pkt.dts = pkt->dts + deltaAudioQ;
					pkt1->pkt.pts = pkt->pts + deltaAudioQ;
					AudioQueueOffset=dimAudioQ-SizeToCopy;
					q->size -= AudioQueueOffset;
					ret = 1;
				}
				else
				{
					AudioQueueOffset=0;
				}
#ifdef DEBUG_QUEUE
				printf("   AudioQueueOffset = %d\n",AudioQueueOffset);
#endif

				//compute lost frame statistics
			if(q->last_frame_extracted > 0 && pkt->stream_index > q->last_frame_extracted) {
#ifdef DEBUG_QUEUE
					printf("  stats: stidx %d last %d tot %d\n", pkt->stream_index, q->last_frame_extracted, q->total_lost_frames);
#endif
					q->total_lost_frames = q->total_lost_frames + pkt->stream_index - q->last_frame_extracted - 1;
				}
				//update index of last frame extracted
				q->last_frame_extracted = pkt->stream_index;
			}
		}
		else {
#ifdef DEBUG_QUEUE
				printf("  AV not 1\n");
#endif
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
				q->last_pkt = NULL;
			q->nb_packets--;
			q->size -= pkt1->pkt.size;
			
			pkt->size = pkt1->pkt.size;
			pkt->dts = pkt1->pkt.dts;
			pkt->pts = pkt1->pkt.pts;
			pkt->stream_index = pkt1->pkt.stream_index;
			pkt->flags = pkt1->pkt.flags;
			pkt->pos = pkt1->pkt.pos;
			pkt->convergence_duration = pkt1->pkt.convergence_duration;
			//*pkt = pkt1->pkt;
#ifdef DEBUG_QUEUE
				printf("  about to free... ");
#endif
			memcpy(pkt->data,pkt1->pkt.data,pkt1->pkt.size);
			free(pkt1->pkt.data);
			av_free(pkt1);
#ifdef DEBUG_QUEUE
				printf("freeed\n");
#endif
			ret = 1;
			//compute lost frame statistics
			if(q->last_frame_extracted > 0 && pkt->stream_index > q->last_frame_extracted) {
#ifdef DEBUG_QUEUE
					printf("  stats: stidx %d last %d tot %d\n", pkt->stream_index, q->last_frame_extracted, q->total_lost_frames);
#endif
					q->total_lost_frames = q->total_lost_frames + pkt->stream_index - q->last_frame_extracted - 1;
			}
			//update index of last frame extracted
			q->last_frame_extracted = pkt->stream_index;
		}
	}
#ifdef DEBUG_QUEUE
	else {
		printf("  pk1 NULLLLLLLLLL!!!!\n");
	}
#endif

	if(q->nb_packets==0 && q->queueType==AUDIO) {
		QueueFillingMode=1;
#ifdef DEBUG_QUEUE
		printf("QUEUE Get FillingMode ON\n");
#endif
	}
#ifdef DEBUG_QUEUE
	printf("QUEUE Get LastFrameExtracted = %d\n",q->last_frame_extracted);
	printf("QUEUE Get Tot lost frames = %d\n",q->total_lost_frames);
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
		FirstTimeAudio=1;
		FirstTime = 1;
		return -1;
	}

	if((FirstTime==1 || FirstTimeAudio==1) && audioq.size>0) {
		if(audioq.first_pkt->pkt.pts>0)
		{
			DeltaTime=Now-(long long)(audioq.first_pkt->pkt.pts);
			FirstTimeAudio = 0;
			FirstTime = 0;
#ifdef DEBUG_AUDIO 
		 	printf("audio_decode_frame - DeltaTimeAudio=%lld\n",DeltaTime);
#endif
		}
	}

#ifdef DEBUG_AUDIO 
	if(audioq.first_pkt)
	{
		printf("audio_decode_frame - Syncro params: DeltaNow-pts=%lld ",(Now-((long long)audioq.first_pkt->pkt.pts+DeltaTime)));
		printf("pts=%lld ",(long long)audioq.first_pkt->pkt.pts);
		printf("Tollerance=%d ",(int)MAX_TOLLERANCE);
		printf("QueueLen=%d ",(int)audioq.nb_packets);
		printf("QueueSize=%d\n",(int)audioq.size);
	}
	else
		printf("audio_decode_frame - Empty queue\n");
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
 		printf("skipaudio: queue size=%d\n",audioq.size);
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
 		printf("Decode audio\n");
#endif
	}

	return audio_pkt_size;

}

/*static long get_time_diff(struct timeval time_now) {
	struct timeval time_now2;
	gettimeofday(&time_now2,0);
	return time_now2.tv_sec*1.e6 - time_now.tv_sec*1.e6 + time_now2.tv_usec - time_now.tv_usec;
}*/

int video_callback(void *valthread) {
	//AVPacket pktvideo;
	AVCodecContext  *pCodecCtx;
        AVCodec         *pCodec;
	AVFrame		*pFrame;
	AVPacket	packet;
	int		frameFinished;
	int 		countexit;
	AVPicture pict;
	static struct SwsContext *img_convert_ctx;
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
        if(avcodec_open(pCodecCtx, pCodec)<0) {
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
			FirstTime = 1;
			usleep(5000);
			continue;
		}

		DecodeVideo = 0;
		SkipVideo = 0;
		//gettimeofday(&now,NULL);
		//Now = (unsigned long long)now.tv_sec*1000+(unsigned long long)now.tv_usec/1000;
		Now=(long long)SDL_GetTicks();
		if(FirstTime==1 && videoq.size>0) {
			if(videoq.first_pkt->pkt.pts>0)
			{
				DeltaTime=Now-(long long)videoq.first_pkt->pkt.pts;
				FirstTime = 0;
			}
#ifdef DEBUG_VIDEO 
		 	printf("VideoCallback - DeltaTimeAudio=%lld\n",DeltaTime);
#endif
		}

#ifdef DEBUG_VIDEO 
		if(videoq.first_pkt)
		{
			printf("VideoCallback - Syncro params: Delta:%lld Now:%lld pts=%lld ",(long long)DeltaTime,Now,(long long)videoq.first_pkt->pkt.pts);
			printf("pts=%lld ",(long long)videoq.first_pkt->pkt.pts);
			printf("Tollerance=%d ",(int)MAX_TOLLERANCE);
			printf("QueueLen=%d ",(int)videoq.nb_packets);
			printf("QueueSize=%d\n",(int)videoq.size);
		}
		else
			printf("VideoCallback - Empty queue\n");
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
		printf("skipvideo:%d decodevideo:%d\n",SkipVideo,DecodeVideo);
#endif

		while(SkipVideo==1 && videoq.size>0) {
			SkipVideo = 0;
#ifdef DEBUG_VIDEO 
 			printf("Skip Video\n");
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
				printf("Decode video FrameTime=%lld Now=%lld\n",(long long)VideoPkt.pts+DeltaTime,Now);
#endif

				avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &VideoPkt);

				if(frameFinished) { // it must be true all the time else error
#ifdef DEBUG_VIDEO
				printf("FrameFinished\n");
#endif
					//SaveFrame(pFrame, pCodecCtx->width, pCodecCtx->height, cont++);
					//fwrite(pktvideo.data, 1, pktvideo.size, frecon);

                        		// Lock SDL_yuv_overlay
	                        	if ( SDL_MUSTLOCK(screen) ) {
#ifdef DEBUG_VIDEO
				printf("-1 ");
#endif

		                        	if ( SDL_LockSurface(screen) < 0 ) {
#ifdef DEBUG_VIDEO
				printf("0 ");
#endif
																break;
															}
                	        	}
#ifdef DEBUG_VIDEO
				printf("1 ");
#endif
                        		if (SDL_LockYUVOverlay(yuv_overlay) < 0) break;
#ifdef DEBUG_VIDEO
				printf("2 ");
#endif

	                        	pict.data[0] = yuv_overlay->pixels[0];
        	                	pict.data[1] = yuv_overlay->pixels[2];
                	        	pict.data[2] = yuv_overlay->pixels[1];

                        		pict.linesize[0] = yuv_overlay->pitches[0];
	                        	pict.linesize[1] = yuv_overlay->pitches[2];
        	                	pict.linesize[2] = yuv_overlay->pitches[1];
#ifdef DEBUG_VIDEO
				printf("3 ");
#endif

                	        	img_convert_ctx = sws_getContext(tval->width,tval->height,PIX_FMT_YUV420P,tval->width,tval->height,PIX_FMT_YUV420P,SWS_BICUBIC,NULL,NULL,NULL);
#ifdef DEBUG_VIDEO
				printf("4 ");
#endif

	                        	if(img_convert_ctx==NULL) {
		                        	fprintf(stderr,"Cannot initialize the conversion context!\n");
        		                        exit(1);
                        		}
	                        	sws_scale(img_convert_ctx,pFrame->data,pFrame->linesize,0,tval->height,pict.data,pict.linesize);
#ifdef DEBUG_VIDEO
				printf("5 ");
#endif

		                        // let's draw the data (*yuv[3]) on a SDL screen (*screen)
        	                	if ( SDL_MUSTLOCK(screen) ) {
			                        SDL_UnlockSurface(screen);
                        		}
#ifdef DEBUG_VIDEO
				printf("6 ");
#endif

                        		SDL_UnlockYUVOverlay(yuv_overlay);

	                        	// Show, baby, show!
        	                	SDL_DisplayYUVOverlay(yuv_overlay, &rect);
#ifdef DEBUG_VIDEO
				printf("7\n");
#endif
			
				}
			}
		}

		usleep(5000);

		/*SDL_PollEvent(&event);
		switch(event.type) {
		case SDL_QUIT:
			quit=1;
			//exit(0);
			break;
		}*/
	}
	av_free(pCodecCtx);
	//fclose(frecon);
#ifdef DEBUG_VIDEO
 	printf("video callback end\n");
#endif
	return 1;
}

/*int audio_decode_frame2(uint8_t *audio_buf,int len) {
	AVPacket pkt;
	if(packet_queue_get(&audioq, &pkt, 1,1) < 0) {
		return -1;
	}
	memcpy(audio_buf,pkt.data,pkt.size);
	//printf("tornato : %d bytes\n",pkt.size);
	return pkt.size;
}*/

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

void ShowBMP(char *file, SDL_Surface *screen, int x, int y) {
	SDL_Surface *image;
	SDL_Rect dest;

	/* Load a BMP file on a surface */
	image = SDL_LoadBMP(file);
	if ( image == NULL ) {
		fprintf(stderr, "Error loading %s: %s\n", file, SDL_GetError());
		return;
	}

	/* Copy on the screen surface 
	surface should be blocked now.
	*/
	dest.x = x;
	dest.y = y;
	dest.w = image->w;
	dest.h = image->h;
	SDL_BlitSurface(image, NULL, screen, &dest);

	/* Update the screen area just changed */
	SDL_UpdateRects(screen, 1, &dest);
}

int alphasortNew(const struct dirent **a, const struct dirent **b) {
	int idx1 = atoi((*a)->d_name+5);
	int idx2 = atoi((*b)->d_name+5);
	return (idx2<idx1);
//	return (strcmp((*a)->d_name,(*b)->d_name));
}

void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame) {
	FILE *pFile;
	char szFilename[32];
	int  y;
  
  // Open file
	sprintf(szFilename, "frame%d.ppm", iFrame);
  pFile=fopen(szFilename, "wb");
  if(pFile==NULL)
    return;
  
  // Write header
  fprintf(pFile, "P5\n%d %d\n255\n", width, height);
  
  // Write pixel data
  for(y=0; y<height; y++)
    fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width, pFile);
  
  // Close file
  fclose(pFile);
}

static void sigint_handler (int signal) {
   printf("Caught SIGINT, exiting...");
   got_sigint = 1;
}

void ProcessKeys()
{
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
	int i, j, videoStream,outbuf_size,out_size,out_size_audio,seq_current_chunk = 0,audioStream;
	int len1, data_size, stime,cont=0;
	int frameFinished, len_audio;
	int numBytes,outbuf_audio_size,audio_size;

	int y;
	
	uint8_t *outbuf,*outbuf_audio;
	uint8_t *outbuf_audi_audio;
	
	AVFormatContext *pFormatCtx;

	AVCodec         *pCodec,*aCodec;
	AVFrame         *pFrame; 

	AVPicture pict;
	static struct SwsContext *img_convert_ctx;
	SDL_Thread *video_thread;//exit_thread,*exit_thread2;
	SDL_Event event;
	//SDL_mutex   *lock;
	SDL_AudioSpec wanted_spec, spec;
	
	struct MHD_Daemon *daemon = NULL;	

	char buf[1024],outfile[1024], basereadfile[1024],readfile[1024];
	FILE *fp;	
//	struct dirent **namelist;
	int width,height,asample_rate,achannels;
	//double framerate;

//  TwBar *bar;

	ThreadVal *tval;
	tval = (ThreadVal *)malloc(sizeof(ThreadVal));
		
	if(argc<5) {
		printf("player width height sample_rate channels\n");
		exit(1);
	}
	sscanf(argv[1],"%d",&width);
	sscanf(argv[2],"%d",&height);
	sscanf(argv[3],"%d",&asample_rate);
	sscanf(argv[4],"%d",&achannels);
	tval->width = width;
	tval->height = height;
	//tval->framerate = framerate;




	
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
	printf("wantedsizeSDL:%d\n",wanted_spec.size);
	if(SDL_OpenAudio(&wanted_spec,&spec)<0) {
		fprintf(stderr,"SDL_OpenAudio: %s\n",SDL_GetError());
		return -1;
	}
	dimAudioQ = spec.size;
	deltaAudioQ = (float)((float)spec.samples)*1000/spec.freq;

	printf("wantedsizeSDL:%d %d\n",wanted_spec.size,wanted_spec.samples);

	printf("freq:%d\n",spec.freq);
	printf("format:%d\n",spec.format);
	printf("channels:%d\n",spec.channels);
	printf("silence:%d\n",spec.silence);
	printf("samples:%d\n",spec.samples);
	printf("size:%d\n",spec.size);

	pFrame=avcodec_alloc_frame();
	if(pFrame==NULL) {
		printf("Memory error!!!\n");
		return -1;
	}
	outbuf_audio = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);

	strcpy(basereadfile,"chunks/");

	packet_queue_init(&audioq,AUDIO);
	packet_queue_init(&videoq,VIDEO);
	SDL_WM_SetCaption("Filling buffer...", NULL);
	// Make a screen to put our video
#ifndef __DARWIN__
	screen = SDL_SetVideoMode(width, height, 0, 0);
#else
	screen = SDL_SetVideoMode(width, height, 24, 0);
#endif
	if(!screen) {
		fprintf(stderr, "SDL: could not set video mode - exiting\n");
		exit(1);
	}

/*
  // Initialize AntTweakBar
  TwInit(TW_OPENGL, NULL);
  // Tell the window size to AntTweakBar
  TwWindowSize(width, height);
  // Create a tweak bar
  bar = TwNewBar("TweakBar");
  // Add 'width' and 'height' to 'bar': they are read-only (RO) variables of type TW_TYPE_INT32.
  TwAddVarRO(bar, "Width", TW_TYPE_INT32, &width, " label='Wnd width' help='Width of the graphics window (in pixels)' ");
*/

	yuv_overlay = SDL_CreateYUVOverlay(width, height,SDL_YV12_OVERLAY, screen);

	if ( yuv_overlay == NULL ) {
		fprintf(stderr,"SDL: Couldn't create SDL_yuv_overlay: %s", SDL_GetError());
		exit(1);
	}

	if ( yuv_overlay->hw_overlay )
		fprintf(stderr,"SDL: Using hardware overlay.");

	rect.x = 0;
	rect.y = 0;
	rect.w = width;
	rect.h = height;

	SDL_DisplayYUVOverlay(yuv_overlay, &rect);

	//signal (SIGINT, sigint_handler);

	// Init audio and video buffers
	av_init_packet(&AudioPkt);
	av_init_packet(&VideoPkt);
	AudioPkt.data=(uint8_t *)malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
	if(!AudioPkt.data) return 0;
	VideoPkt.data=(uint8_t *)malloc(width*height*3/2);
	if(!VideoPkt.data) return 0;
	

	SDL_PauseAudio(0);
	video_thread = SDL_CreateThread(video_callback,tval);


	//lock = SDL_CreateMutex();
	//SDL_WaitThread(exit_thread2,NULL);



	daemon = initChunkPuller();



	// Wait for user input
	while(!quit)
	{
		if(QueueFillingMode)
			SDL_WM_SetCaption("Filling buffer...", NULL);
		else
			SDL_WM_SetCaption("NAPA-Wine Player", NULL);
		SDL_PollEvent(&event);
		switch(event.type) {
		case SDL_QUIT:
			//exit(0);
			quit=1;
			break;
		}
		ProcessKeys();
		usleep(20000);
	}
	// Stop audio&video playback

printf("1\n");	
	SDL_WaitThread(video_thread,NULL);
printf("2\n");	
	SDL_PauseAudio(1);
printf("3\n");	
	SDL_CloseAudio();
printf("4\n");	
	SDL_Quit();

	//SDL_DestroyMutex(lock);
printf("5\n");	
	av_free(aCodecCtx);
printf("6\n");	
	free(AudioPkt.data);
printf("7\n");	
	free(VideoPkt.data);
//	free(namelist);
printf("8\n");	
  free(outbuf_audio);

printf("9\n");	
	finalizeChunkPuller(daemon);
printf("10\n");	

	return 0;
}



int enqueueBlock(const uint8_t *block, const int block_size) {
	Chunk *gchunk=NULL;
  ExternalChunk *echunk=NULL;
	uint8_t *tempdata, *buffer;
  int i, j;
	Frame *frame=NULL;
	AVPacket packet, packetaudio;

	uint8_t *video_bufQ = NULL;
	uint16_t *audio_bufQ = NULL;
	//uint8_t audio_bufQ[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
	int16_t *dataQ;
	int data_sizeQ;
	int lenQ;
	int sizeFrame = 0;
	sizeFrame = 3*sizeof(int)+sizeof(struct timeval);

	audio_bufQ = (uint16_t *)av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
	gchunk = (Chunk *)malloc(sizeof(Chunk));
	if(!gchunk) {
		printf("Memory error in gchunk!\n");
		return PLAYER_FAIL_RETURN;
	}

  decodeChunk(gchunk, block, block_size);

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
		printf("Memory error!\n");
		return -1;
	}

		tempdata = echunk->data;
		j=echunk->payload_len;
		while(j>0 && !quit) {
			//usleep(30000);
			frame->number = *((int *)tempdata);
			tempdata+=sizeof(int);
			frame->timestamp = *((struct timeval *)tempdata);
			tempdata += sizeof(struct timeval);
			frame->size = *((int *)tempdata);
			tempdata+=sizeof(int);
			frame->type = *((int *)tempdata);
			tempdata+=sizeof(int);

			buffer = tempdata; // here coded frame information
			tempdata+=frame->size;
			//printf("%d %d %d %d\n",frame->number,frame->timestamp.tv_usec,frame->size,frame->type);

			if(frame->type!=5) { // video frame
				av_init_packet(&packet);
				video_bufQ = (uint8_t *)malloc(frame->size); //this gets freed at the time of packet_queue_get
				if(video_bufQ) {
					memcpy(video_bufQ, buffer, frame->size);
					packet.data = video_bufQ;
					packet.size = frame->size;
					packet.pts = frame->timestamp.tv_sec*(unsigned long long)1000+frame->timestamp.tv_usec;
					packet.dts = frame->timestamp.tv_sec*(unsigned long long)1000+frame->timestamp.tv_usec;
					packet.stream_index = frame->number; // use of stream_index for number frame
					//packet.duration = frame->timestamp.tv_sec;
					packet_queue_put(&videoq,&packet);
#ifdef DEBUG_SOURCE
					printf("SOURCE: Insert video in queue pts=%lld %d %d sindex:%d\n",packet.pts,(int)frame->timestamp.tv_sec,(int)frame->timestamp.tv_usec,packet.stream_index);
#endif
				}
			}
			else { // audio frame
				av_init_packet(&packetaudio);
				packetaudio.data = buffer;
				packetaudio.size = frame->size;

				packetaudio.pts = frame->timestamp.tv_sec*(unsigned long long)1000+frame->timestamp.tv_usec;
				packetaudio.dts = frame->timestamp.tv_sec*(unsigned long long)1000+frame->timestamp.tv_usec;
				//packetaudio.duration = frame->timestamp.tv_sec;
				packetaudio.stream_index = 1;
				packetaudio.flags = 1;
				packetaudio.pos = -1;
				packetaudio.convergence_duration = -1;

				// insert the audio frame into the queue
				data_sizeQ = AVCODEC_MAX_AUDIO_FRAME_SIZE;
        lenQ = avcodec_decode_audio3(aCodecCtx, (int16_t *)audio_bufQ, &data_sizeQ, &packetaudio);
				if(lenQ>0)
				{
					// for freeing there is some memory still in tempdata to be freed
					dataQ = (int16_t *)malloc(data_sizeQ); //this gets freed at the time of packet_queue_get
					if(dataQ)
					{
						memcpy(dataQ,audio_bufQ,data_sizeQ);
						packetaudio.data = (int8_t *)dataQ;
						packetaudio.size = data_sizeQ;
						packetaudio.stream_index = frame->number; // use of stream_index for number frame
		
						packet_queue_put(&audioq,&packetaudio);
#ifdef DEBUG_SOURCE
						printf("SOURCE: Insert audio in queue pts=%lld\n",packetaudio.pts);
#endif
					}
				}

			}
			j = j - sizeFrame - frame->size;
		}

	
/*
		if(QueueFillingMode)
			SDL_WM_SetCaption("Filling buffer...", NULL);
		else
			SDL_WM_SetCaption("NAPA-Wine Player", NULL);
		SDL_PollEvent(&event);
		switch(event.type) {
		case SDL_QUIT:
			//exit(0);
			quit=1;
			break;
		}
		ProcessKeys();
*/
  free(echunk->data);
	free(echunk);
	free(frame);
  av_free(audio_bufQ);
}
