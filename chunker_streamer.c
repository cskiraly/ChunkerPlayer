// ingestion.c
// Author 
// Diego Reforgiato
// Giuseppe Tropea
//
// Use the file compile to compile the program to build (assuming libavformat and libavcodec are 
// correctly installed your system).
//
// Run using
//
// ingestion myvideofile.mpg

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <stdio.h>

#include <SDL.h>
#include <SDL_thread.h>

#include "chunker_streamer.h"
#include "codec_definitions.h"

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#define DEBUG_AUDIO_FRAMES
#define DEBUG_VIDEO_FRAMES
//#define DEBUG_CHUNKER

#define SDL_AUDIO_BUFFER_SIZE 1024

int alphasortNew(const struct dirent **a, const struct dirent **b) {
	int idx1 = atoi((*a)->d_name+5);
	int idx2 = atoi((*b)->d_name+5);
	return (idx2<idx1);
//	return (strcmp((*a)->d_name,(*b)->d_name));
}

int chunkFilled(ExternalChunk *echunk, ChunkerMetadata *cmeta) {
	// different strategies to implement
	if(cmeta->strategy==0) // number of frames per chunk constant
		if(echunk->frames_num==cmeta->val_strategy)
			return 1;
	
	if(cmeta->strategy==1) // constant size. Note that for now each chunk will have a size just greater or equal than the required value - It can be considered as constant size. If that is not good we need to change the code. Also, to prevent too low values of strategy_val. This choice is more robust
		if(echunk->payload_len>=cmeta->val_strategy)
			return 1;
	
	return 0;
	
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

void saveChunkOnFile(ExternalChunk *chunk) {
	char buf[1024], outfile[1024];
/*	FILE *fp;
	
	strcpy(buf,"chunks//CHUNK");
	strcat(buf,"\0");
	sprintf(outfile,"%s%d",buf,chunk->seq);
	fp = fopen(outfile,"wb");
	fwrite(&(chunk->seq),sizeof(int),1,fp);
	fwrite(&(chunk->frames_num),sizeof(int),1,fp);
	fwrite(&(chunk->start_time),sizeof(struct timeval),1,fp);
	fwrite(&(chunk->end_time),sizeof(struct timeval),1,fp);
	fwrite(&(chunk->payload_len),sizeof(int),1,fp);
	fwrite(&(chunk->len),sizeof(int),1,fp);
	fwrite(&(chunk->category),sizeof(int),1,fp);
	fwrite(&(chunk->priority),sizeof(double),1,fp);
	fwrite(&(chunk->_refcnt),sizeof(int),1,fp);
	fwrite(chunk->data,sizeof(uint8_t),sizeof(uint8_t)*chunk->payload_len,fp);
	fclose(fp);
*/
	/* send the chunk to the GRAPES peer application via HTTP */
	sprintf(buf, "%slocalhost:%d%s", UL_HTTP_PREFIX, UL_DEFAULT_CHUNKBUFFER_PORT, UL_DEFAULT_CHUNKBUFFER_PATH);
	pushChunkHttp(chunk, buf);
}

void initChunk(ExternalChunk *chunk,int *seq_num) {
	chunk->seq = (*seq_num)++;
	chunk->frames_num = 0;
	chunk->payload_len = 0;
	chunk->len=0;
  if(chunk->data != NULL)
    free(chunk->data);
	chunk->data = NULL;
	chunk->start_time.tv_sec = -1;
	chunk->start_time.tv_usec = -1;
	chunk->end_time.tv_sec = -1;
	chunk->end_time.tv_usec = -1;
	chunk->priority = 0;
	chunk->category = 0;
	chunk->_refcnt = 0;
}

int main(int argc, char *argv[]) {
	int i, videoStream,outbuf_size,out_size,seq_current_chunk = 0,audioStream;
	int len1, data_size, stime;
	int frameFinished;
	int numBytes,outbuf_audio_size,audio_size;
	int sizeFrame = 0;
	int sizeChunk = 0;
	int dir_entries;
	int audio_bitrate;
	int video_bitrate;
	int contFrameAudio=0,contFrameVideo=0;
	
	uint8_t *buffer,*outbuf,*outbuf_audio;
	uint8_t *outbuf_audi_audio,*tempdata;
	//uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
	uint16_t *audio_buf = NULL;

	unsigned int audio_buf_size = 0;
	long double newtimestamp;
	
	AVFormatContext *pFormatCtx;
	AVCodecContext  *pCodecCtx,*pCodecCtxEnc,*aCodecCtxEnc,*aCodecCtx;
	AVCodec         *pCodec,*pCodecEnc,*aCodec,*aCodecEnc;
	AVFrame         *pFrame; 
	AVFrame         *pFrameRGB;
	AVPacket         packet;
	int64_t		last_pkt_dts=0,delta=0,delta_audio=0,last_pkt_dts_audio=0;

	Frame *frame=NULL;

	ExternalChunk *chunk=NULL;
	ExternalChunk *chunkaudio=NULL;
	ChunkerMetadata *cmeta=NULL;
	
	char buf[1024],outfile[1024], basedelfile[1024],delfile[1024];
	FILE *fp,*f1;

	long long Now;
	short int FirstTimeAudio=1, FirstTimeVideo=1;
	long long DeltaTimeAudio=0, DeltaTimeVideo=0, newTime;

	double ptsvideo1,ptsaudio1;
	
	struct dirent **namelist;
	
	if(argc < 4) {
		fprintf(stderr, "execute ./chunker_streamer moviefile audiobitrate videobitrate\n");
		return -1;
	}
	sscanf(argv[2],"%d",&audio_bitrate);
	sscanf(argv[3],"%d",&video_bitrate);

	audio_buf = (uint16_t *)av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
	outbuf_audio_size = 10000;
	outbuf_audio = malloc(outbuf_audio_size);


	cmeta = chunkerInit("configChunker.txt");
	
/*
	dir_entries = scandir("chunks",&namelist,NULL,alphasortNew);
	if(dir_entries>0) {
		strcpy(basedelfile,"chunks/");
		for(i=0;i<dir_entries;i++) {
			if(!strcmp(namelist[i]->d_name,".") || !strcmp(namelist[i]->d_name,".."))
				continue;
			strcpy(delfile,basedelfile);
			strcat(delfile,namelist[i]->d_name);
			unlink(delfile);
			free(namelist[i]);
		}
		free(namelist);
		rmdir("chunks");
	}
	mkdir("chunks",0777);
*/
	
	fprintf(stderr, "Chunkbuffer Strategy:%d Value Strategy:%d\n", cmeta->strategy, cmeta->val_strategy);
	f1 = fopen("original.mpg","wb");

	// Register all formats and codecs
	av_register_all();

	// Open video file
	if(av_open_input_file(&pFormatCtx, argv[1], NULL, 0, NULL)!=0)
		return -1; // Couldn't open file
  
	// Retrieve stream information
	if(av_find_stream_info(pFormatCtx)<0)
		return -1; // Couldn't find stream information
  
	// Dump information about file onto standard error
	dump_format(pFormatCtx, 0, argv[1], 0);
  
	// Find the first video stream
	videoStream=-1;
	audioStream=-1;
	
	for(i=0; i<pFormatCtx->nb_streams; i++) {
		if(pFormatCtx->streams[i]->codec->codec_type==CODEC_TYPE_VIDEO && videoStream<0) {
			videoStream=i;
		}
		if(pFormatCtx->streams[i]->codec->codec_type==CODEC_TYPE_AUDIO && audioStream<0) {
			audioStream=i;
		}
	}
	fprintf(stderr, "Num streams : %d TBR: %d %d RFRAMERATE:%d %d Duration:%d\n",pFormatCtx->nb_streams,pFormatCtx->streams[videoStream]->time_base.num,pFormatCtx->streams[videoStream]->time_base.den,pFormatCtx->streams[videoStream]->r_frame_rate.num,pFormatCtx->streams[videoStream]->r_frame_rate.den,pFormatCtx->streams[videoStream]->duration);

	fprintf(stderr, "Video stream has id : %d\n",videoStream);
	fprintf(stderr, "Audio stream has id : %d\n",audioStream);

	if(videoStream==-1 && audioStream==-1)
		return -1; // Didn't find a video stream
  
	// Get a pointer to the codec context for the video stream
	pCodecCtx=pFormatCtx->streams[videoStream]->codec;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);

	fprintf(stderr, "Width:%d Height:%d\n",pCodecCtx->width,pCodecCtx->height);

	if(audioStream!=-1) {
		aCodecCtx=pFormatCtx->streams[audioStream]->codec;
		fprintf(stderr, "AUDIO Codecid: %d %d\n",aCodecCtx->codec_id,aCodecCtx->sample_rate);
		fprintf(stderr, "AUDIO channels %d samplerate %d\n",aCodecCtx->channels,aCodecCtx->sample_rate);
	}



	pCodecCtxEnc=avcodec_alloc_context();
#ifdef H264_VIDEO_ENCODER
	pCodecCtxEnc->me_range=16;
	pCodecCtxEnc->max_qdiff=4;
	pCodecCtxEnc->qmin=10;
	pCodecCtxEnc->qmax=51;
	pCodecCtxEnc->qcompress=0.6;
	pCodecCtxEnc->codec_type = CODEC_TYPE_VIDEO;
	pCodecCtxEnc->codec_id   = CODEC_ID_H264;//13;//pCodecCtx->codec_id;
	pCodecCtxEnc->bit_rate = video_bitrate;///400000;
	// resolution must be a multiple of two 
	pCodecCtxEnc->width = pCodecCtx->width;
	pCodecCtxEnc->height = pCodecCtx->height;
	// frames per second 
	pCodecCtxEnc->time_base= pCodecCtx->time_base;//(AVRational){1,25};
	pCodecCtxEnc->gop_size = 10; // emit one intra frame every ten frames 
	//pCodecCtxEnc->max_b_frames=1;
	pCodecCtxEnc->pix_fmt = PIX_FMT_YUV420P;
#else
	pCodecCtxEnc->codec_type = CODEC_TYPE_VIDEO;
	pCodecCtxEnc->codec_id   = CODEC_ID_MPEG4;
	pCodecCtxEnc->bit_rate = video_bitrate;
	pCodecCtxEnc->width = pCodecCtx->width;
	pCodecCtxEnc->height = pCodecCtx->height;
	// frames per second 
	pCodecCtxEnc->time_base= pCodecCtx->time_base;//(AVRational){1,25};
	pCodecCtxEnc->gop_size = 10; // emit one intra frame every ten frames 
	//pCodecCtxEnc->max_b_frames=1;
	pCodecCtxEnc->pix_fmt = PIX_FMT_YUV420P;
#endif
	fprintf(stderr, "CIC:%d %d %d %d\n", pCodecCtxEnc->time_base.num, pCodecCtxEnc->time_base.den, pCodecCtx->time_base.num, pCodecCtx->time_base.den);


	aCodecCtxEnc = avcodec_alloc_context();
	aCodecCtxEnc->bit_rate = audio_bitrate; //256000
	aCodecCtxEnc->sample_fmt = SAMPLE_FMT_S16;
	aCodecCtxEnc->sample_rate = aCodecCtx->sample_rate;
	aCodecCtxEnc->channels = aCodecCtx->channels;
        //fprintf(stderr, "InitAUDIOFRAMESIZE:%d %d\n",aCodecCtxEnc->frame_size,av_rescale(44100,1,25));
	fprintf(stderr, "bitrate:%d sample_rate:%d channels:%d\n",aCodecCtxEnc->bit_rate,aCodecCtxEnc->sample_rate,aCodecCtxEnc->channels);

	// Find the decoder for the video stream
	
	if(audioStream!=-1) {
		aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
#ifdef MP3_AUDIO_ENCODER
		aCodecEnc = avcodec_find_encoder(CODEC_ID_MP3);
#else
		aCodecEnc = avcodec_find_encoder(CODEC_ID_MP2);
#endif
		if(aCodec==NULL) {
			fprintf(stderr,"Unsupported acodec!\n");
			return -1;
		}
		if(aCodecEnc==NULL) {
			fprintf(stderr,"Unsupported acodecEnc!\n");
			return -1;
		}
	
		if(avcodec_open(aCodecCtx, aCodec)<0) {
			fprintf(stderr, "could not open codec\n");
			return -1; // Could not open codec
		}
		if(avcodec_open(aCodecCtxEnc, aCodecEnc)<0) {
			fprintf(stderr, "could not open codec\n");
			return -1; // Could not open codec
		}

	}
#ifdef H264_VIDEO_ENCODER
	fprintf(stderr, "Setting codecID to H264: %d %d\n",pCodecCtx->codec_id, CODEC_ID_H264);
	pCodecEnc = avcodec_find_encoder(CODEC_ID_H264);//pCodecCtx->codec_id);
#else
	fprintf(stderr, "Setting codecID to mpeg4: %d %d\n",pCodecCtx->codec_id, CODEC_ID_MPEG4);
	pCodecEnc = avcodec_find_encoder(CODEC_ID_MPEG4);
#endif
	if(pCodec==NULL) {
		fprintf(stderr, "Unsupported pcodec!\n");
		return -1; // Codec not found
	}
	if(pCodecEnc==NULL) {
		fprintf(stderr, "Unsupported pcodecenc!\n");
		return -1; // Codec not found
	}
	if(avcodec_open(pCodecCtx, pCodec)<0) {
		fprintf(stderr, "could not open codec\n");
		return -1; // Could not open codec
	}
	if(avcodec_open(pCodecCtxEnc, pCodecEnc)<0) {
		fprintf(stderr, "could not open codecEnc\n");
		return -1; // Could not open codec
	}

	// Allocate video frame
	pFrame=avcodec_alloc_frame();
	if(pFrame==NULL) {
		fprintf(stderr, "Memory error!!!\n");
		return -1;
	}
  
	i=0;
	outbuf_size = 100000;
	outbuf = malloc(outbuf_size);
	if(!outbuf) {
		fprintf(stderr, "Memory error!!!\n");
		return -1;
	}
	frame = (Frame *)malloc(sizeof(Frame));
	if(!frame) {
		fprintf(stderr, "Memory error!!!\n");
		return -1;
	}
	sizeFrame = 3*sizeof(int)+sizeof(struct timeval);
	chunk = (ExternalChunk *)malloc(sizeof(ExternalChunk));
	if(!chunk) {
		fprintf(stderr, "Memory error!!!\n");
		return -1;
	}
	sizeChunk = 6*sizeof(int)+2*sizeof(struct timeval)+sizeof(double);
    chunk->data=NULL;
	initChunk(chunk,&seq_current_chunk);
	chunkaudio = (ExternalChunk *)malloc(sizeof(ExternalChunk));
	if(!chunkaudio) {
		fprintf(stderr, "Memory error!!!\n");
		return -1;
	}
    chunkaudio->data=NULL;
	initChunk(chunkaudio,&seq_current_chunk);
	stime = -1;
	
	//av_init_packet(&packet);
	
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}


	/* initialize the HTTP chunk pusher */
	initChunkPusher(); //TRIPLO


	while(av_read_frame(pFormatCtx, &packet)>=0) {
		// Is this a packet from the video stream?
		if(packet.stream_index==videoStream) {
			// Decode video frame
			double PTS,glob_PTS;

			PTS = 0;
			glob_PTS = packet.pts;
			if(avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet)>0) {
#ifdef DEBUG_VIDEO_FRAMES
				fprintf(stderr, "Tipo Frame: %d %lld\n",pFrame->pict_type,AV_NOPTS_VALUE);
#endif
/*				if(packet.dts==AV_NOPTS_VALUE && pFrame->opaque && *(uint64_t*)pFrame->opaque!=AV_NOPTS_VALUE) {
					PTS = *(uint64_t *)pFrame->opaque;
				}
				else if(packet.dts!=AV_NOPTS_VALUE) {
					PTS = packet.dts;
				}
				else
					PTS = 0;
				PTS *= av_q2d(pCodecCtx->time_base);*/

				// Did we get a video frame?
				//stime = (int)packet.dts;
#ifdef DEBUG_VIDEO_FRAMES
				fprintf(stderr, "VIDEO: stime DTS:%d %d\n",stime,(int)packet.dts);
#endif
				if(frameFinished) { // it must be true all the time else error
					frame->number = contFrameVideo++;//pCodecCtx->frame_number;
#ifdef DEBUG_VIDEO_FRAMES
					fprintf(stderr, "DENTROFRAMEFINISHED %lf\n",PTS);
#endif
					if(frame->number==0) {
						if(packet.dts==AV_NOPTS_VALUE)
							continue;
						last_pkt_dts = packet.dts;
						newTime = 0;
					}
					else {
						if(packet.dts!=AV_NOPTS_VALUE) {
							delta = packet.dts-last_pkt_dts;
							last_pkt_dts = packet.dts;
						}
						else if(delta==0)
							continue;
					}
#ifdef DEBUG_VIDEO_FRAMES
					fprintf(stderr, "deltavideo : %lld\n",delta);
#endif
					out_size = avcodec_encode_video(pCodecCtxEnc, outbuf, outbuf_size, pFrame);
#ifdef DEBUG_VIDEO_FRAMES
					fprintf(stderr, "video frame size nuovo : %d\n",pCodecCtx->frame_number);
#endif
					
					Now=(long long)SDL_GetTicks();
#ifdef DEBUG_VIDEO_FRAMES
					fprintf(stderr, "ptsvideo:%lld %d %d %d %f %d\n", packet.pts, (int)packet.duration, pCodecCtxEnc->time_base.den, pCodecCtxEnc->time_base.num, (double)packet.dts, pCodecCtx->time_base.den);
#endif

/*					if(packet.duration>0)
						newtimestamp = ((double)((packet.dts)*pCodecCtxEnc->time_base.num))/((double)(packet.duration*(pCodecCtxEnc->time_base.den)));
					else
						newtimestamp =((double)((packet.dts)*pCodecCtxEnc->time_base.num)/((double)pCodecCtxEnc->time_base.den));
					//frame->timestamp.tv_sec = (long)packet.dts*1.e-5;
					//frame->timestamp.tv_usec = ((long)packet.dts%100000/100);
#ifdef DEBUG_VIDEO_FRAMES
					//fprintf(stderr, "ts:%llf\n",newtimestamp);
#endif
					if(FirstTimeVideo) {
						DeltaTimeVideo=Now-((unsigned int)newtimestamp*(unsigned long long)1000+(newtimestamp-(unsigned int)newtimestamp)*1000);
						FirstTimeVideo = 0;
#ifdef DEBUG_VIDEO_FRAMES
						//fprintf(stderr, "DeltaTimeVideo : %lld\n",DeltaTimeVideo);
						//printf("%lld\n",Now);
#endif
					}
	
					newTime = DeltaTimeVideo+((unsigned int)newtimestamp*(unsigned long long)1000+(newtimestamp-(unsigned int)newtimestamp)*1000);
*/
/*					char countzeros[10];
					int countz=0,ic;
					sprintf(countzeros,"%d",pCodecCtxEnc->time_base.den);
					for(ic=0;ic<strlen(countzeros);ic++) {

						if(countzeros[ic]=='0')
							countz++;
					}*/

					//newTime = (((double)((packet.dts)-dtsvideo1))*1000.0*(double)pCodecCtxEnc->time_base.num)/(pCodecCtxEnc->time_base.den*((double)(packet.duration)));						
					
					if(FirstTimeVideo && packet.pts>0) {
						ptsvideo1 = packet.pts;
						FirstTimeVideo = 0;
					}

					if(frame->number>0)
						newTime = ((double)packet.pts-ptsvideo1)*1000.0/((double)delta*(double)av_q2d(pFormatCtx->streams[videoStream]->r_frame_rate));
					
					//newTime = ((double)((packet.dts)))/(1000*av_q2d(pCodecCtxEnc->time_base));
					
#ifdef DEBUG_VIDEO_FRAMES
					fprintf(stderr, "newTime : %d\n",newTime);
#endif
					if(newTime<0)
						continue; //SKIP THIS FRAME (E.G. INTERLACE)
	
					frame->timestamp.tv_sec = (long long)newTime/1000;
					frame->timestamp.tv_usec = newTime%1000;
	
					frame->size = out_size;
					frame->type = pFrame->pict_type;
#ifdef DEBUG_VIDEO_FRAMES
					//fprintf(stderr, "video newt:%lf dts:%ld dur:%d sec:%d usec%d pts:%d\n",newtimestamp,(long)packet.dts,packet.duration,frame->timestamp.tv_sec,frame->timestamp.tv_usec,packet.pts);
					fprintf(stderr, "num:%d size:%d type:%d\n",frame->number,frame->size,frame->type);
					fprintf(stderr, "VIDEO: tvsec:%d tvusec:%d\n",frame->timestamp.tv_sec,frame->timestamp.tv_usec);
#endif
					// Save the frame to disk
					++i;
					//SaveFrame(pFrame, pCodecCtx->width, pCodecCtx->height, i);
#ifdef DEBUG_VIDEO_FRAMES	
					//fprintf(stderr, "out_size:%d outbuf_size:%d packet.size:%d\n",out_size,outbuf_size,packet.size);
					//fprintf(stderr, "%d %d\n",pCodecCtxEnc->width,pCodecCtxEnc->height);
#endif
					chunk->frames_num++; // number of frames in the current chunk
					chunk->data = (uint8_t *)realloc(chunk->data,sizeof(uint8_t)*(chunk->payload_len+out_size+sizeFrame));
					if(!chunk->data)  {
						fprintf(stderr, "Memory error!!!\n");
						return -1;
					}
#ifdef DEBUG_VIDEO_FRAMES
					//fprintf(stderr, "rialloco data di dim:%d con nuova dim:%d\n",chunk->payload_len,out_size);
#endif

					tempdata = chunk->data+chunk->payload_len;
					*((int *)tempdata) = frame->number;
					tempdata+=sizeof(int);
					*((struct timeval *)tempdata) = frame->timestamp;
					tempdata+=sizeof(struct timeval);
					*((int *)tempdata) = frame->size;
					tempdata+=sizeof(int);
					*((int *)tempdata) = frame->type;
					tempdata+=sizeof(int);
					
					memcpy(chunk->data+chunk->payload_len+sizeFrame,outbuf,out_size); // insert new data
					chunk->payload_len += out_size + sizeFrame; // update payload length
					//fprintf(stderr, "outsize:%d payload_len:%d\n",out_size,chunk->payload_len);
					chunk->len = sizeChunk+chunk->payload_len ; // update overall length
					
					if(((int)frame->timestamp.tv_sec < (int)chunk->start_time.tv_sec) || ((int)frame->timestamp.tv_sec==(int)chunk->start_time.tv_sec && (int)frame->timestamp.tv_usec < (int)chunk->start_time.tv_usec) || (int)chunk->start_time.tv_sec==-1) {
						chunk->start_time.tv_sec = frame->timestamp.tv_sec;
						chunk->start_time.tv_usec = frame->timestamp.tv_usec;
					}
					if(((int)frame->timestamp.tv_sec > (int)chunk->end_time.tv_sec) || ((int)frame->timestamp.tv_sec==(int)chunk->end_time.tv_sec && (int)frame->timestamp.tv_usec > (int)chunk->end_time.tv_usec) || (int)chunk->end_time.tv_sec==-1) {
						chunk->end_time.tv_sec = frame->timestamp.tv_sec;
						chunk->end_time.tv_usec = frame->timestamp.tv_usec;
					}
					stime = -1;
	
					//fprintf(stderr, "Frame video %d stime:%d frame num:%d\n",stime,pCodecCtxEnc->frame_number);
					if(chunkFilled(chunk,cmeta)) { // is chunk filled using current strategy?
						//chbAddChunk(chunkbuffer,chunk); // add a chunk to the chunkbuffer
						//SAVE ON FILE
						saveChunkOnFile(chunk);
						initChunk(chunk,&seq_current_chunk);
					}
					/* pict_type maybe 1 (I), 2 (P), 3 (B), 5 (AUDIO)*/
	
					fwrite(outbuf, 1, out_size, f1); // reconstructing original video
	
				}
			}
		}
		else if(packet.stream_index==audioStream) {
#ifdef DEBUG_AUDIO_FRAMES
			//fprintf(stderr, "packet audio dts:%d dts:%d\n",(int)packet.dts,(int)packet.dts);
#endif
			data_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
			if(avcodec_decode_audio3(aCodecCtx, audio_buf, &data_size, &packet)>0) {
#ifdef DEBUG_AUDIO_FRAMES
				fprintf(stderr, "newTimeaudioSTART : %lf\n",(double)(packet.pts)*av_q2d(pFormatCtx->streams[audioStream]->time_base));
#endif
				if(data_size>0) {
#ifdef DEBUG_AUDIO_FRAMES
					fprintf(stderr, "datasizeaudio:%d\n",data_size);
#endif
					/* if a frame has been decoded, output it */
					//fwrite(audio_buf, 1, data_size, outfileaudio);
				}
				else
					printf("Error data_size!!\n");
	
				audio_size = avcodec_encode_audio(aCodecCtxEnc,outbuf_audio,data_size,audio_buf);
#ifdef DEBUG_AUDIO_FRAMES
				//fprintf(stderr, "audio frame size nuovo pippo : %d %d datasize:%d %d %d\n",aCodecCtxEnc->frame_size,aCodecCtxEnc->time_base.den,data_size,packet.size, len1);
				//fprintf(stderr, "oldaudiosize:%d Audio size : %d\n",len1,audio_size);
				//fprintf(stderr, "stream_index:%d flags:%d pos:%d conv_duration:%d dts:%d dts:%d duration:%d\n",packet.stream_index,packet.flags,packet.pos,packet.convergence_duration,packet.dts,packet.dts,packet.duration);
#endif				
				frame->number = contFrameAudio++;//aCodecCtx->frame_number;
				if(frame->number==0) {
					if(packet.dts==AV_NOPTS_VALUE)
						continue;
					last_pkt_dts_audio = packet.dts;
					newTime = 0;
				}
				else {
					if(packet.dts!=AV_NOPTS_VALUE) {
						delta_audio = packet.dts-last_pkt_dts_audio;
						last_pkt_dts_audio = packet.dts;
					}
					else if(delta_audio==0)
						continue;
				}

				//newtimestamp = (double)((float)packet.dts/(packet.duration*34.100));
				Now=(long long)SDL_GetTicks();
/*
				if(packet.duration>0)
					newtimestamp = ((double)((packet.dts)*aCodecCtxEnc->time_base.num))/((double)(packet.duration*(aCodecCtxEnc->time_base.den)));
				else
					newtimestamp =((double)((packet.dts)*aCodecCtxEnc->time_base.num)/((double)aCodecCtxEnc->time_base.den));
				newtimestamp = ((double)((packet.dts)*aCodecCtxEnc->time_base.num))/((double)(aCodecCtxEnc->time_base.den));
#ifdef DEBUG_AUDIO_FRAMES
				fprintf(stderr, "ts:%llf\n",newtimestamp);
				//frame->timestamp.tv_sec = (long)packet.dts*1.e-5;
				//frame->timestamp.tv_usec = ((long)packet.dts%100000/100);
	
				fprintf(stderr, "dtsaudio:%f %d %d %d\n",(double)packet.dts,(int)packet.duration,aCodecCtxEnc->time_base.den,aCodecCtxEnc->time_base.num);
#endif
				if(FirstTimeAudio) {
					DeltaTimeAudio=Now-((unsigned int)newtimestamp*(unsigned long long)1000+(newtimestamp-(unsigned int)newtimestamp)*1000);
					FirstTimeAudio = 0;
				}
	
				newTime = DeltaTimeAudio+((unsigned int)newtimestamp*(unsigned long long)1000+(newtimestamp-(unsigned int)newtimestamp)*1000);
#ifdef DEBUG_AUDIO_FRAMES
				//fprintf(stderr, "%lld %lld\n",newTime/1000,newTime%1000);
#endif
*/
				if(FirstTimeAudio && packet.pts>0) {
					ptsaudio1 = packet.pts;
					FirstTimeAudio = 0;
				}

				if(frame->number>0)
					newTime = (((double)packet.pts-ptsaudio1)*1000.0*((double)av_q2d(pFormatCtx->streams[audioStream]->time_base)));//*(double)delta_audio;
			
//				newTime = (aCodecCtx->sample_rate/(2*aCodecCtx->channels))*(((double)((packet.pts)-ptsaudio1))/((double)(packet.duration)))*1000.0*(double)pFormatCtx->streams[audioStream]->time_base.num/(double)pFormatCtx->streams[audioStream]->time_base.den;
//				newTime = (double)(packet.pts)*av_q2d(pFormatCtx->streams[audioStream]->time_base);

	
				frame->timestamp.tv_sec = (unsigned int)newTime/1000;
				frame->timestamp.tv_usec = newTime%1000;
#ifdef DEBUG_AUDIO
				fprintf(stderr, "ptsaudio:%f %d %d %d %f\n",(double)packet.pts,(int)packet.duration,pFormatCtx->streams[audioStream]->time_base.num,pFormatCtx->streams[audioStream]->time_base.den,(double)packet.dts);
				//fprintf(stderr, "newTio :  %lf\n",(((double)((packet.pts)-ptsaudio1))/((double)(packet.duration)))*1000.0*(double)pFormatCtx->streams[audioStream]->time_base.num/(double)pFormatCtx->streams[audioStream]->time_base.den);

				fprintf(stderr, "AUDIO: tvsec:%d tvusec:%d\n",frame->timestamp.tv_sec,frame->timestamp.tv_usec);
				fprintf(stderr, "newTimeaudio : %d\n",newTime);
				fprintf(stderr, "deltaaudio : %lld\n",delta_audio);
				
#endif
				//frame->timestamp.tv_sec = (unsigned int)newtimestamp;
				//frame->timestamp.tv_usec = (newtimestamp-frame->timestamp.tv_sec)*1000;
	
				//fprintf(stderr, "Audio time nuovo :%lld %lld\n",Now,DeltaTimeAudio+((long long)frame->timestamp.tv_sec*(unsigned long long)1000+frame->timestamp.tv_usec));
	
				//fprintf(stderr, "audio newt:%lf dts:%ld dur:%d sec:%d usec%d pts:%d\n",newtimestamp,(long)packet.dts,packet.duration,frame->timestamp.tv_sec,frame->timestamp.tv_usec,packet.pts);
				frame->size = audio_size;
				frame->type = 5; // 5 is audio type
				
				chunkaudio->frames_num++; // number of frames in the current chunk
				
				chunkaudio->data = (uint8_t *)realloc(chunkaudio->data,sizeof(uint8_t)*(chunkaudio->payload_len+audio_size+sizeFrame));
				if(!chunkaudio->data) {
					fprintf(stderr, "Memory error!!!\n");
					return -1;
				}
				tempdata = chunkaudio->data+chunkaudio->payload_len;
				*((int *)tempdata) = frame->number;
				tempdata+=sizeof(int);
				*((struct timeval *)tempdata) = frame->timestamp;
				tempdata+=sizeof(struct timeval);
				*((int *)tempdata) = frame->size;
				tempdata+=sizeof(int);
				*((int *)tempdata) = frame->type;
				tempdata+=sizeof(int);
					
				memcpy(chunkaudio->data+chunkaudio->payload_len+sizeFrame,outbuf_audio,audio_size);
				chunkaudio->payload_len += audio_size + sizeFrame; // update payload length
					//fprintf(stderr, "outsize:%d payload_len:%d\n",out_size,chunk->payload_len);
				chunkaudio->len = sizeChunk+chunkaudio->payload_len ; // update overall length
				
	//			stime = (int)packet.dts;
				//fprintf(stderr, "AUDIO: stime:%d\n",stime);
				if(((int)frame->timestamp.tv_sec < (int)chunkaudio->start_time.tv_sec) || ((int)frame->timestamp.tv_sec==(int)chunkaudio->start_time.tv_sec && (int)frame->timestamp.tv_usec < (int)chunkaudio->start_time.tv_usec) || (int)chunkaudio->start_time.tv_sec==-1) {
					chunkaudio->start_time.tv_sec = frame->timestamp.tv_sec;
					chunkaudio->start_time.tv_usec = frame->timestamp.tv_usec;
				}
				if(((int)frame->timestamp.tv_sec > (int)chunkaudio->end_time.tv_sec) || ((int)frame->timestamp.tv_sec==(int)chunkaudio->end_time.tv_sec && (int)frame->timestamp.tv_usec > (int)chunkaudio->end_time.tv_usec) || (int)chunkaudio->end_time.tv_sec==-1) {
					chunkaudio->end_time.tv_sec = frame->timestamp.tv_sec;
					chunkaudio->end_time.tv_usec = frame->timestamp.tv_usec;
				}
		//		if(stime < chunkaudio->start_time.tv_usec || chunkaudio->start_time.tv_usec==-1)
			//		chunkaudio->start_time.tv_usec = stime;
			//	if(stime > chunkaudio->end_time.tv_usec || chunkaudio->end_time.tv_usec==-1)
			//		chunkaudio->end_time.tv_usec = stime;
				stime=-1;
				chunkaudio->priority = 1;
#ifdef DEBUG_AUDIO_FRAMES
				//fprintf(stderr, "Frame audio stime:%d frame_num:%d\n",stime,aCodecCtxEnc->frame_number);
#endif
				if(chunkFilled(chunkaudio,cmeta)) { // is chunk filled using current strategy?
					//chbAddChunk(chunkbuffer,chunkaudio); // add a chunk to the chunkbuffer
					//SAVE ON FILE
					saveChunkOnFile(chunkaudio);
					initChunk(chunkaudio,&seq_current_chunk);
				}
#ifdef DEBUG_AUDIO_FRAMES
				//fprintf(stderr,"readingQUIPEPPE3\n");
#endif
			}
		}
		else {
#ifdef DEBUG_AUDIO_FRAMES
			fprintf(stderr,"Free the packet that was allocated by av_read_frame\n");
#endif
			av_free_packet(&packet);
		}
	}

	if(chunk->frames_num>0) {
		//chbAddChunk(chunkbuffer,chunk);
		//SAVE ON FILE
		saveChunkOnFile(chunk);
	}
	if(chunkaudio->frames_num>0) {
		//chbAddChunk(chunkbuffer,chunkaudio);
		//SAVE ON FILE
		saveChunkOnFile(chunkaudio);
		
	}


	/* initialize the HTTP chunk pusher */
	finalizeChunkPusher();


	free(chunk);
	free(chunkaudio);
	free(frame);
	free(outbuf);
	free(outbuf_audio);
	fclose(f1);

	// Writing chunk files
#ifdef DEBUG_CHUNKER
	fprintf(stderr, "cmetasize:%d\n",cmeta->size);
	for(i=0;i<cmeta->size;i++) {
		fprintf(stderr, "seq:%d pay_len:%d frames_num:%d priority:%f ssec:%d susec:%d esec:%d eusec:%d\n",cmeta->echunk[i].seq,cmeta->echunk[i].payload_len,cmeta->echunk[i].frames_num,cmeta->echunk[i].priority,(int)cmeta->echunk[i].start_time.tv_sec,(int)cmeta->echunk[i].start_time.tv_usec,(int)cmeta->echunk[i].end_time.tv_sec,(int)cmeta->echunk[i].end_time.tv_usec);
	}
#endif

	// Free the YUV frame
	av_free(pFrame);
	av_free(audio_buf);
  
	// Close the codec
	avcodec_close(pCodecCtx);
	avcodec_close(pCodecCtxEnc);

	if(audioStream!=-1) {
		avcodec_close(aCodecCtx);
		avcodec_close(aCodecCtxEnc);
	}
  
	// Close the video file
	av_close_input_file(pFormatCtx);
  
    SDL_Quit();
	return 0;
}
