// chunker_streamer.c
// Author 
// Diego Reforgiato
// Giuseppe Tropea
// Dario Marchese
// Carmelo Daniele
//
// Use the file compile.localffmpeg.static to build the program


#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <stdio.h>

#include "chunker_streamer.h"
#include "codec_definitions.h"

//#define DEBUG_AUDIO_FRAMES
//#define DEBUG_VIDEO_FRAMES
//#define DEBUG_CHUNKER
//#define DEBUG_TIME


/*
int alphasortNew(const struct dirent **a, const struct dirent **b) {
	int idx1 = atoi((*a)->d_name+5);
	int idx2 = atoi((*b)->d_name+5);
	return (idx2<idx1);
//	return (strcmp((*a)->d_name,(*b)->d_name));
}
*/

ChunkerMetadata *cmeta=NULL;

int chunkFilled(ExternalChunk *echunk, ChunkerMetadata *cmeta) {
	// different strategies to implement
	if(cmeta->strategy == 0) // number of frames per chunk constant
		if(echunk->frames_num == cmeta->val_strategy)
			return 1;
	
	if(cmeta->strategy == 1) // constant size. Note that for now each chunk will have a size just greater or equal than the required value - It can be considered as constant size. If that is not good we need to change the code. Also, to prevent too low values of strategy_val. This choice is more robust
		if(echunk->payload_len >= cmeta->val_strategy)
			return 1;
	
	return 0;
}

/*
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
*/

/*
void saveChunkOnFile(ExternalChunk *chunk) {
	char buf[1024], outfile[1024];
	FILE *fp;
	
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
}
*/

void initChunk(ExternalChunk *chunk, int *seq_num) {
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
	int i=0;
	int videoStream, outbuf_size, out_size, seq_current_chunk = 0, audioStream; //HINT MORE BYTES IN SEQ
	int len1, data_size;
	int frameFinished;
	int numBytes, outbuf_audio_size, audio_size;
	int sizeFrame = 0;
	int sizeChunk = 0;
	int dir_entries;
	int audio_bitrate;
	int video_bitrate;
	int contFrameAudio=0, contFrameVideo=0;
	int live_source=0;
	
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
	int64_t last_pkt_dts=0, delta_video=0, delta_audio=0, last_pkt_dts_audio=0, target_pts=0;

	Frame *frame=NULL;

	ExternalChunk *chunk=NULL;
	ExternalChunk *chunkaudio=NULL;
	
	char buf[1024], outfile[1024], basedelfile[1024], delfile[1024];
	short int FirstTimeAudio=1, FirstTimeVideo=1;
	long long newTime;

	double ptsvideo1=0.0;
	double ptsaudio1=0.0;
	
//	struct dirent **namelist;
	
	if(argc < 4) {
		fprintf(stderr, "execute ./chunker_streamer moviefile audiobitrate videobitrate <live source flag (0 or 1)>\n");
		return -1;
	}
	sscanf(argv[2],"%d", &audio_bitrate);
	sscanf(argv[3],"%d", &video_bitrate);
	if(argc==5) sscanf(argv[4],"%d", &live_source);

	// read the configuration file
	cmeta = chunkerInit();
	if(live_source)
		fprintf(stderr, "INIT: Using LIVE SOURCE TimeStamps\n");

	audio_buf = (uint16_t *)av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
	outbuf_audio_size = 10000;
	outbuf_audio = malloc(outbuf_audio_size);

	// Register all formats and codecs
	av_register_all();

	// Open video file
	if(av_open_input_file(&pFormatCtx, argv[1], NULL, 0, NULL) != 0) {
		fprintf(stdout, "INIT: Couldn't open video file. Exiting.\n");
		exit(-1);
	}

	// Retrieve stream information
	if(av_find_stream_info(pFormatCtx) < 0) {
		fprintf(stdout, "INIT: Couldn't find stream information. Exiting.\n");
		exit(-1);
	}

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
	fprintf(stderr, "INIT: Num streams : %d TBR: %d %d RFRAMERATE:%d %d Duration:%d\n", pFormatCtx->nb_streams, pFormatCtx->streams[videoStream]->time_base.num, pFormatCtx->streams[videoStream]->time_base.den, pFormatCtx->streams[videoStream]->r_frame_rate.num, pFormatCtx->streams[videoStream]->r_frame_rate.den, pFormatCtx->streams[videoStream]->duration);

	fprintf(stderr, "INIT: Video stream has id : %d\n",videoStream);
	fprintf(stderr, "INIT: Audio stream has id : %d\n",audioStream);

	if(videoStream==-1 && audioStream==-1) {
		fprintf(stdout, "INIT: Didn't find audio and video streams. Exiting.\n");
		exit(-1);
	}

	// Get a pointer to the codec context for the video stream
	pCodecCtx=pFormatCtx->streams[videoStream]->codec;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);

	fprintf(stderr, "INIT: Width:%d Height:%d\n",pCodecCtx->width,pCodecCtx->height);

	if(audioStream!=-1) {
		aCodecCtx=pFormatCtx->streams[audioStream]->codec;
		fprintf(stderr, "INIT: AUDIO Codecid: %d channels %d samplerate %d\n", aCodecCtx->codec_id, aCodecCtx->channels, aCodecCtx->sample_rate);
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
	fprintf(stderr, "INIT: VIDEO timebase OUT:%d %d IN: %d %d\n", pCodecCtxEnc->time_base.num, pCodecCtxEnc->time_base.den, pCodecCtx->time_base.num, pCodecCtx->time_base.den);


	aCodecCtxEnc = avcodec_alloc_context();
	aCodecCtxEnc->bit_rate = audio_bitrate; //256000
	aCodecCtxEnc->sample_fmt = SAMPLE_FMT_S16;
	aCodecCtxEnc->sample_rate = aCodecCtx->sample_rate;
	aCodecCtxEnc->channels = aCodecCtx->channels;
        //fprintf(stderr, "InitAUDIOFRAMESIZE:%d %d\n",aCodecCtxEnc->frame_size,av_rescale(44100,1,25));
	fprintf(stderr, "INIT: AUDIO bitrate OUT:%d sample_rate:%d channels:%d\n", aCodecCtxEnc->bit_rate, aCodecCtxEnc->sample_rate, aCodecCtxEnc->channels);

	// Find the decoder for the video stream
	
	if(audioStream!=-1) {
		aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
#ifdef MP3_AUDIO_ENCODER
		aCodecEnc = avcodec_find_encoder(CODEC_ID_MP3);
#else
		aCodecEnc = avcodec_find_encoder(CODEC_ID_MP2);
#endif
		if(aCodec==NULL) {
			fprintf(stderr,"INIT: Unsupported acodec!\n");
			return -1;
		}
		if(aCodecEnc==NULL) {
			fprintf(stderr,"INIT: Unsupported acodecEnc!\n");
			return -1;
		}
	
		if(avcodec_open(aCodecCtx, aCodec)<0) {
			fprintf(stderr, "INIT: could not open IN AUDIO codec\n");
			return -1; // Could not open codec
		}
		if(avcodec_open(aCodecCtxEnc, aCodecEnc)<0) {
			fprintf(stderr, "INIT: could not open OUT AUDIO codec\n");
			return -1; // Could not open codec
		}

	}
#ifdef H264_VIDEO_ENCODER
	fprintf(stderr, "INIT: Setting VIDEO codecID to H264: %d %d\n",pCodecCtx->codec_id, CODEC_ID_H264);
	pCodecEnc = avcodec_find_encoder(CODEC_ID_H264);//pCodecCtx->codec_id);
#else
	fprintf(stderr, "INIT: Setting VIDEO codecID to mpeg4: %d %d\n",pCodecCtx->codec_id, CODEC_ID_MPEG4);
	pCodecEnc = avcodec_find_encoder(CODEC_ID_MPEG4);
#endif
	if(pCodec==NULL) {
		fprintf(stderr, "INIT: Unsupported IN VIDEO pcodec!\n");
		return -1; // Codec not found
	}
	if(pCodecEnc==NULL) {
		fprintf(stderr, "INIT: Unsupported OUT VIDEO pcodecenc!\n");
		return -1; // Codec not found
	}
	if(avcodec_open(pCodecCtx, pCodec)<0) {
		fprintf(stderr, "INIT: could not open IN VIDEO codec\n");
		return -1; // Could not open codec
	}
	if(avcodec_open(pCodecCtxEnc, pCodecEnc)<0) {
		fprintf(stderr, "INIT: could not open OUT VIDEO codecEnc\n");
		return -1; // Could not open codec
	}

	// Allocate video frame
	pFrame=avcodec_alloc_frame();
	if(pFrame==NULL) {
		fprintf(stderr, "INIT: Memory error alloc video frame!!!\n");
		return -1;
	}
  
	i=0;
	outbuf_size = 100000;
	outbuf = malloc(outbuf_size);
	if(!outbuf) {
		fprintf(stderr, "INIT: Memory error alloc outbuf!!!\n");
		return -1;
	}
	frame = (Frame *)malloc(sizeof(Frame));
	if(!frame) {
		fprintf(stderr, "INIT: Memory error alloc Frame!!!\n");
		return -1;
	}
	sizeFrame = 3*sizeof(int32_t)+sizeof(struct timeval);
	chunk = (ExternalChunk *)malloc(sizeof(ExternalChunk));
	if(!chunk) {
		fprintf(stderr, "INIT: Memory error alloc chunk!!!\n");
		return -1;
	}
	sizeChunk = 6*sizeof(int32_t)+2*sizeof(struct timeval)+sizeof(double);
    chunk->data=NULL;
	initChunk(chunk, &seq_current_chunk);
	chunkaudio = (ExternalChunk *)malloc(sizeof(ExternalChunk));
	if(!chunkaudio) {
		fprintf(stderr, "INIT: Memory error alloc chunkaudio!!!\n");
		return -1;
	}
    chunkaudio->data=NULL;
	initChunk(chunkaudio, &seq_current_chunk+1);
	
	//av_init_packet(&packet);

	/* initialize the HTTP chunk pusher */
	initChunkPusher(); //TRIPLO


	while(av_read_frame(pFormatCtx, &packet)>=0) {
		// Is this a packet from the video stream?
		if(packet.stream_index==videoStream) {
			// Decode video frame
			if(avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet)>0) {
#ifdef DEBUG_VIDEO_FRAMES
				fprintf(stderr, "-------VIDEO FRAME type %d\n", pFrame->pict_type);
				fprintf(stderr, "VIDEO: dts %lld pts %lld\n", packet.dts, packet.pts);
#endif
				if(frameFinished) { // it must be true all the time else error
					frame->number = contFrameVideo;
#ifdef DEBUG_VIDEO_FRAMES
					fprintf(stderr, "VIDEO: finished frame %d dts %lld pts %lld\n", frame->number, packet.dts, packet.pts);
#endif
					if(frame->number==0) {
						if(packet.dts==AV_NOPTS_VALUE)
							//a Dts with a noPts value is troublesome case for delta calculation based on Dts
							continue;
						last_pkt_dts = packet.dts;
						newTime = 0;
					}
					else {
						if(packet.dts!=AV_NOPTS_VALUE) {
							delta_video = packet.dts-last_pkt_dts;
/*
							DeltaTimeVideo=((double)packet.dts-(double)last_pkt_dts)*1000.0/((double)delta_video*(double)av_q2d(pFormatCtx->streams[videoStream]->r_frame_rate));
							if(DeltaTimeVideo<0) DeltaTimeVideo=10;
							if(DeltaTimeVideo>80) DeltaTimeVideo=80;
*/
							last_pkt_dts = packet.dts;
						}
						else if(delta_video==0)
							//a Dts with a noPts value is troublesome case for delta calculation based on Dts
							continue;
					}
#ifdef DEBUG_VIDEO_FRAMES
					fprintf(stderr, "VIDEO: deltavideo : %d\n", (int)delta_video);
#endif
					out_size = avcodec_encode_video(pCodecCtxEnc, outbuf, outbuf_size, pFrame);
#ifdef DEBUG_VIDEO_FRAMES
					fprintf(stderr, "VIDEO: original codec frame number %d\n", pCodecCtx->frame_number);
					fprintf(stderr, "VIDEO: duration %d timebase %d %d container timebase %d\n", (int)packet.duration, pCodecCtxEnc->time_base.den, pCodecCtxEnc->time_base.num, pCodecCtx->time_base.den);
#endif

					//use pts if dts is invalid
					if(packet.dts!=AV_NOPTS_VALUE)
						target_pts = packet.dts;
					else if(packet.pts!=AV_NOPTS_VALUE)
						target_pts = packet.pts;
					else
						continue;

					if(!live_source)
					{
						if(FirstTimeVideo && packet.pts>0) {
							ptsvideo1 = (double)packet.dts;
							FirstTimeVideo = 0;
#ifdef DEBUG_VIDEO_FRAMES
							fprintf(stderr, "VIDEO: SET PTS BASE OFFSET %f\n", ptsvideo1);
#endif
						}
						if(frame->number>0) {
							//if(ptsaudio1>0)
								//use audio-based timestamps when available (both for video and audio frames)
								//newTime = (((double)target_pts-ptsaudio1)*1000.0*((double)av_q2d(pFormatCtx->streams[audioStream]->time_base)));//*(double)delta_audio;
							//else
								newTime = ((double)target_pts-ptsvideo1)*1000.0/((double)delta_video*(double)av_q2d(pFormatCtx->streams[videoStream]->r_frame_rate));
						}
					}
					else //live source
					{
						if(FirstTimeVideo && packet.dts>0) {
							//maintain the offset between audio pts and video pts
							//because in case of live source they have the same numbering
							if(ptsaudio1 > 0) //if we have already seen some audio frames...
								ptsvideo1 = ptsaudio1;
							else
								ptsvideo1 = (double)packet.dts;
							FirstTimeVideo = 0;
#ifdef DEBUG_VIDEO_FRAMES
							fprintf(stderr, "VIDEO LIVE: SET PTS BASE OFFSET %f\n", ptsvideo1);
#endif
						}
						if(frame->number>0) {
							newTime = ((double)target_pts-ptsvideo1)*1000.0/((double)delta_video*(double)av_q2d(pFormatCtx->streams[videoStream]->r_frame_rate));
						}
						//this was for on-the-fly timestamping
						//newTime=Now-StartTime;
					}
#ifdef DEBUG_VIDEO_FRAMES
					fprintf(stderr, "VIDEO: NEWTIMESTAMP %ld\n", newTime);
#endif
					if(newTime<0) {
#ifdef DEBUG_VIDEO_FRAMES
						fprintf(stderr, "VIDEO: SKIPPING FRAME\n");
#endif
						continue; //SKIP THIS FRAME, bad timestamp
					}
	
					frame->timestamp.tv_sec = (long long)newTime/1000;
					frame->timestamp.tv_usec = newTime%1000;
	
					frame->size = out_size;
					frame->type = pFrame->pict_type;
#ifdef DEBUG_VIDEO_FRAMES
					fprintf(stderr, "VIDEO: encapsulated frame num:%d size:%d type:%d\n", frame->number, frame->size, frame->type);
					fprintf(stderr, "VIDEO: timestamped sec %d usec:%d\n", frame->timestamp.tv_sec, frame->timestamp.tv_usec);
					//fprintf(stderr, "out_size:%d outbuf_size:%d packet.size:%d\n",out_size,outbuf_size,packet.size);
#endif
					// Save the frame to disk
					//++i;
					//SaveFrame(pFrame, pCodecCtx->width, pCodecCtx->height, i);
					//HINT on malloc
					chunk->data = (uint8_t *)realloc(chunk->data, sizeof(uint8_t)*(chunk->payload_len+out_size+sizeFrame));
					if(!chunk->data)  {
						fprintf(stderr, "Memory error in chunk!!!\n");
						return -1;
					}
					chunk->frames_num++; // number of frames in the current chunk
					//lets increase the numbering of the frames
					contFrameVideo++;
#ifdef DEBUG_VIDEO_FRAMES
					//fprintf(stderr, "rialloco data di dim:%d con nuova dim:%d\n",chunk->payload_len,out_size);
#endif

					tempdata = chunk->data+chunk->payload_len;
					*((int32_t *)tempdata) = frame->number;
					tempdata+=sizeof(int32_t);
					*((struct timeval *)tempdata) = frame->timestamp;
					tempdata+=sizeof(struct timeval);
					*((int32_t *)tempdata) = frame->size;
					tempdata+=sizeof(int32_t);
					*((int32_t *)tempdata) = frame->type;
					tempdata+=sizeof(int32_t);
					
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
	
					if(chunkFilled(chunk, cmeta)) { // is chunk filled using current strategy?
						//SAVE ON FILE
						//saveChunkOnFile(chunk);
						//Send the chunk via http to an external transport/player
						pushChunkHttp(chunk, cmeta->outside_world_url);
						initChunk(chunk, &seq_current_chunk);
					}
					/* pict_type maybe 1 (I), 2 (P), 3 (B), 5 (AUDIO)*/
				}
			}
		}
		else if(packet.stream_index==audioStream) {
			data_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
			if(avcodec_decode_audio3(aCodecCtx, audio_buf, &data_size, &packet)>0) {
#ifdef DEBUG_AUDIO_FRAMES
				fprintf(stderr, "\n-------AUDIO FRAME\n");
				fprintf(stderr, "AUDIO: newTimeaudioSTART : %lf\n", (double)(packet.pts)*av_q2d(pFormatCtx->streams[audioStream]->time_base));
#endif
				if(data_size>0) {
#ifdef DEBUG_AUDIO_FRAMES
					fprintf(stderr, "AUDIO: datasizeaudio:%d\n", data_size);
#endif
					/* if a frame has been decoded, output it */
					//fwrite(audio_buf, 1, data_size, outfileaudio);
				}
				else
					continue;
	
				audio_size = avcodec_encode_audio(aCodecCtxEnc,outbuf_audio,data_size,audio_buf);
				frame->number = contFrameAudio;

				if(frame->number==0) {
					if(packet.dts==AV_NOPTS_VALUE)
						continue;
					last_pkt_dts_audio = packet.dts;
					newTime = 0;
				}
				else {
					if(packet.dts!=AV_NOPTS_VALUE) {
						delta_audio = packet.dts-last_pkt_dts_audio;
/*
						DeltaTimeAudio=(((double)packet.dts-last_pkt_dts_audio)*1000.0*((double)av_q2d(pFormatCtx->streams[audioStream]->time_base)));
						if(DeltaTimeAudio<0) DeltaTimeAudio=10;
						if(DeltaTimeAudio>1500) DeltaTimeAudio=1500;
*/
						last_pkt_dts_audio = packet.dts;
					}
					else if(delta_audio==0)
						continue;
				}

				//use pts if dts is invalid
				if(packet.dts!=AV_NOPTS_VALUE)
					target_pts = packet.dts;
				else if(packet.pts!=AV_NOPTS_VALUE)
					target_pts = packet.pts;
				else
					continue;

				if(!live_source)
				{
					if(FirstTimeAudio && packet.pts>0) {
						//maintain the offset between audio pts and video pts
						//because in case of live source they have the same numbering
						if(ptsvideo1 > 0) //if we have already seen some video frames...
							ptsaudio1 = ptsvideo1;
						else
							ptsaudio1 = (double)packet.dts;
						FirstTimeAudio = 0;
#ifdef DEBUG_AUDIO_FRAMES
						fprintf(stderr, "AUDIO: SET PTS BASE OFFSET %f\n", ptsaudio1);
#endif
					}
					if(frame->number>0) {
							if(ptsaudio1>0)
								//use audio-based timestamps when available (both for video and audio frames)
								newTime = (((double)target_pts-ptsaudio1)*1000.0*((double)av_q2d(pFormatCtx->streams[audioStream]->time_base)));//*(double)delta_audio;
							else
								newTime = ((double)target_pts-ptsvideo1)*1000.0/((double)delta_video*(double)av_q2d(pFormatCtx->streams[videoStream]->r_frame_rate));
					}
				}
				else //live source
				{
					if(FirstTimeAudio && packet.pts>0) {
						//maintain the offset between audio pts and video pts
						//because in case of live source they have the same numbering
						if(ptsvideo1 > 0) //if we have already seen some video frames...
							ptsaudio1 = ptsvideo1;
						else
							ptsaudio1 = (double)packet.dts;
						FirstTimeAudio = 0;
#ifdef DEBUG_AUDIO_FRAMES
						fprintf(stderr, "AUDIO LIVE: SET PTS BASE OFFSET %f\n", ptsaudio1);
#endif
					}

					if(frame->number>0) {
							//if(ptsaudio1>0)
								//use audio-based timestamps when available (both for video and audio frames)
								newTime = (((double)target_pts-ptsaudio1)*1000.0*((double)av_q2d(pFormatCtx->streams[audioStream]->time_base)));//*(double)delta_audio;
							//else
							//	newTime = ((double)target_pts-ptsvideo1)*1000.0/((double)delta_video*(double)av_q2d(pFormatCtx->streams[videoStream]->r_frame_rate));
					}
					//this was for on-the-fly timestamping
					//newTime=Now-StartTime;
				}
#ifdef DEBUG_AUDIO_FRAMES
				fprintf(stderr, "AUDIO: NEWTIMESTAMP %d\n", newTime);
#endif
				if(newTime<0) {
#ifdef DEBUG_AUDIO_FRAMES
					fprintf(stderr, "AUDIO: SKIPPING FRAME\n");
#endif
					continue; //SKIP THIS FRAME, bad timestamp
				}

				frame->timestamp.tv_sec = (unsigned int)newTime/1000;
				frame->timestamp.tv_usec = newTime%1000;
#ifdef DEBUG_AUDIO_FRAMES
				fprintf(stderr, "AUDIO: pts %d duration %d timebase %d %d dts %d\n", (int)packet.pts, (int)packet.duration, pFormatCtx->streams[audioStream]->time_base.num, pFormatCtx->streams[audioStream]->time_base.den, (int)packet.dts);
				fprintf(stderr, "AUDIO: timestamp sec:%d usec:%d\n", frame->timestamp.tv_sec, frame->timestamp.tv_usec);
				fprintf(stderr, "AUDIO: deltaaudio %lld\n", delta_audio);	
#endif

				frame->size = audio_size;
				frame->type = 5; // 5 is audio type

				chunkaudio->data = (uint8_t *)realloc(chunkaudio->data,sizeof(uint8_t)*(chunkaudio->payload_len+audio_size+sizeFrame));
				if(!chunkaudio->data) {
					fprintf(stderr, "Memory error AUDIO chunk!!!\n");
					return -1;
				}
				chunkaudio->frames_num++; // number of frames in the current chunk
				contFrameAudio++;
				tempdata = chunkaudio->data+chunkaudio->payload_len;
				*((int32_t *)tempdata) = frame->number;
				tempdata+=sizeof(int32_t);
				*((struct timeval *)tempdata) = frame->timestamp;
				tempdata+=sizeof(struct timeval);
				*((int32_t *)tempdata) = frame->size;
				tempdata+=sizeof(int32_t);
				*((int32_t *)tempdata) = frame->type;
				tempdata+=sizeof(int32_t);
					
				memcpy(chunkaudio->data+chunkaudio->payload_len+sizeFrame,outbuf_audio,audio_size);
				chunkaudio->payload_len += audio_size + sizeFrame; // update payload length
					//fprintf(stderr, "outsize:%d payload_len:%d\n",out_size,chunk->payload_len);
				chunkaudio->len = sizeChunk+chunkaudio->payload_len ; // update overall length
				
				if(((int)frame->timestamp.tv_sec < (int)chunkaudio->start_time.tv_sec) || ((int)frame->timestamp.tv_sec==(int)chunkaudio->start_time.tv_sec && (int)frame->timestamp.tv_usec < (int)chunkaudio->start_time.tv_usec) || (int)chunkaudio->start_time.tv_sec==-1) {
					chunkaudio->start_time.tv_sec = frame->timestamp.tv_sec;
					chunkaudio->start_time.tv_usec = frame->timestamp.tv_usec;
				}
				if(((int)frame->timestamp.tv_sec > (int)chunkaudio->end_time.tv_sec) || ((int)frame->timestamp.tv_sec==(int)chunkaudio->end_time.tv_sec && (int)frame->timestamp.tv_usec > (int)chunkaudio->end_time.tv_usec) || (int)chunkaudio->end_time.tv_sec==-1) {
					chunkaudio->end_time.tv_sec = frame->timestamp.tv_sec;
					chunkaudio->end_time.tv_usec = frame->timestamp.tv_usec;
				}

				//set priority
				chunkaudio->priority = 1;

				if(chunkFilled(chunkaudio, cmeta)) { // is chunk filled using current strategy?
					//SAVE ON FILE
					//saveChunkOnFile(chunkaudio);
					//Send the chunk via http to an external transport/player
					pushChunkHttp(chunkaudio, cmeta->outside_world_url);
					initChunk(chunkaudio, &seq_current_chunk+1);
				}
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
		//SAVE ON FILE
		//saveChunkOnFile(chunk);
		//Send the chunk via http to an external transport/player
		pushChunkHttp(chunk, cmeta->outside_world_url);
	}
	if(chunkaudio->frames_num>0) {
		//SAVE ON FILE
		//saveChunkOnFile(chunkaudio);
		//Send the chunk via http to an external transport/player
		pushChunkHttp(chunkaudio, cmeta->outside_world_url);
	}

	/* finalize the HTTP chunk pusher */
	finalizeChunkPusher();

	free(chunk);
	free(chunkaudio);
	free(frame);
	free(outbuf);
	free(outbuf_audio);
	free(cmeta);

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
	return 0;
}
