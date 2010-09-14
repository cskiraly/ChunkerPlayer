// chunker_streamer.c
// Author 
// Diego Reforgiato
// Giuseppe Tropea
// Dario Marchese
// Carmelo Daniele
//
// Use the file compile.localffmpeg.static to build the program


#include "chunker_streamer.h"


//#define DEBUG_AUDIO_FRAMES
//#define DEBUG_VIDEO_FRAMES
//#define DEBUG_CHUNKER
#define DEBUG_ANOMALIES
//#define DEBUG_TIMESTAMPING

#define MAX(a,b) ((a>b)?(a):(b))

ChunkerMetadata *cmeta = NULL;
int seq_current_chunk = 1; //chunk numbering starts from 1; HINT do i need more bytes?


int chunkFilled(ExternalChunk *echunk, ChunkerMetadata *cmeta) {
	// different strategies to implement
	if(cmeta->strategy == 0) { // number of frames per chunk constant
#ifdef DEBUG_CHUNKER
		fprintf(stderr, "CHUNKER: check if frames num %d == %d in chunk %d\n", echunk->frames_num, cmeta->val_strategy, echunk->seq);
#endif
		if(echunk->frames_num == cmeta->val_strategy)
			return 1;
  }
	
	if(cmeta->strategy == 1) // constant size. Note that for now each chunk will have a size just greater or equal than the required value - It can be considered as constant size. If that is not good we need to change the code. Also, to prevent too low values of strategy_val. This choice is more robust
		if(echunk->payload_len >= cmeta->val_strategy)
			return 1;
	
	return 0;
}


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

	//output variables
	uint8_t *video_outbuf = NULL;
	int video_outbuf_size, video_frame_size;
	uint8_t *audio_outbuf = NULL;
	int audio_outbuf_size, audio_frame_size;
	int audio_data_size;

	//numeric identifiers of input streams
	int videoStream = -1;
	int audioStream = -1;

	int len1;
	int frameFinished;
	//frame sequential counters
	int contFrameAudio=1, contFrameVideo=1;
	int numBytes;

	//command line parameters
	int audio_bitrate;
	int video_bitrate;
	int live_source = 0; //tells to sleep before reading next frame in not live (i.e. file)
	int offset_av = 0; //tells to compensate for offset between audio and video in the file
	
	//a raw buffer for decoded uncompressed audio samples
	int16_t *samples = NULL;
	//a raw uncompressed video picture
	AVFrame *pFrame = NULL;

	AVFormatContext *pFormatCtx;
	AVCodecContext  *pCodecCtx,*pCodecCtxEnc,*aCodecCtxEnc,*aCodecCtx;
	AVCodec         *pCodec,*pCodecEnc,*aCodec,*aCodecEnc;
	AVPacket         packet;

	//stuff needed to compute the right timestamps
	short int FirstTimeAudio=1, FirstTimeVideo=1;
	short int pts_anomalies_counter=0;
	short int newtime_anomalies_counter=0;
	long long newTime=0, newTime_audio=0, newTime_video=0, newTime_prev=0;
	struct timeval lastAudioSent = {0, 0};
	double ptsvideo1=0.0;
	double ptsaudio1=0.0;
	int64_t last_pkt_dts=0, delta_video=0, delta_audio=0, last_pkt_dts_audio=0, target_pts=0;

	//Napa-Wine specific Frame and Chunk structures for transport
	Frame *frame = NULL;
	ExternalChunk *chunk = NULL;
	ExternalChunk *chunkaudio = NULL;


	//scan the command line
	if(argc < 4) {
		fprintf(stderr, "execute ./chunker_streamer moviefile audiobitrate videobitrate <live source flag (0 or 1)> <offset av flag (0 or 1)>\n");
		return -1;
	}
	sscanf(argv[2],"%d", &audio_bitrate);
	sscanf(argv[3],"%d", &video_bitrate);
	if(argc>=5) sscanf(argv[4],"%d", &live_source);
	if(argc==6) sscanf(argv[5],"%d", &offset_av);

restart:
	// read the configuration file
	cmeta = chunkerInit();
	if(live_source)
		fprintf(stderr, "INIT: Using LIVE SOURCE TimeStamps\n");
	if(offset_av)
		fprintf(stderr, "INIT: Compensating AV OFFSET in file\n");

	// Register all formats and codecs
	av_register_all();

	// Open input file
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

	// Find the video and audio stream numbers
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

	// Get a pointer to the codec context for the input video stream
	pCodecCtx=pFormatCtx->streams[videoStream]->codec;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	//extract W and H
	fprintf(stderr, "INIT: Width:%d Height:%d\n", pCodecCtx->width, pCodecCtx->height);

	// Get a pointer to the codec context for the input audio stream
	if(audioStream != -1) {
		aCodecCtx=pFormatCtx->streams[audioStream]->codec;
		fprintf(stderr, "INIT: AUDIO Codecid: %d channels %d samplerate %d\n", aCodecCtx->codec_id, aCodecCtx->channels, aCodecCtx->sample_rate);
	}

	//setup video output encoder
	pCodecCtxEnc=avcodec_alloc_context();
#ifdef H264_VIDEO_ENCODER
	pCodecCtxEnc->me_range=16;
	pCodecCtxEnc->max_qdiff=4;
	pCodecCtxEnc->qmin=1;
	pCodecCtxEnc->qmax=30;
	pCodecCtxEnc->qcompress=0.6;
	pCodecCtxEnc->codec_type = CODEC_TYPE_VIDEO;
	pCodecCtxEnc->codec_id   = CODEC_ID_H264;//13;//pCodecCtx->codec_id;
	pCodecCtxEnc->bit_rate = video_bitrate;///400000;
	// resolution must be a multiple of two 
	pCodecCtxEnc->width = pCodecCtx->width;
	pCodecCtxEnc->height = pCodecCtx->height;
	// frames per second 
	pCodecCtxEnc->time_base= pCodecCtx->time_base;//(AVRational){1,25};
	pCodecCtxEnc->gop_size = 100; // emit one intra frame every ten frames 
	//pCodecCtxEnc->max_b_frames=1;
	pCodecCtxEnc->pix_fmt = PIX_FMT_YUV420P;

	pCodecCtxEnc->bit_rate_tolerance = video_bitrate*50;
//	pCodecCtxEnc->rc_min_rate = 0;
//	pCodecCtxEnc->rc_max_rate = 0;
//	pCodecCtxEnc->rc_buffer_size = 0;
//	pCodecCtxEnc->flags |= CODEC_FLAG_PSNR;
//	pCodecCtxEnc->partitions = X264_PART_I4X4 | X264_PART_I8X8 | X264_PART_P8X8 | X264_PART_P4X4 | X264_PART_B8X8;
//	pCodecCtxEnc->crf = 0.0f;
#else
	pCodecCtxEnc->codec_type = CODEC_TYPE_VIDEO;
	pCodecCtxEnc->codec_id   = CODEC_ID_MPEG4;
	pCodecCtxEnc->bit_rate = video_bitrate;
	//times 20 follows the defaults, was not needed in previous versions of libavcodec
	pCodecCtxEnc->bit_rate_tolerance = video_bitrate*20;
//	pCodecCtxEnc->crf = 20.0f;
	pCodecCtxEnc->width = pCodecCtx->width;
	pCodecCtxEnc->height = pCodecCtx->height;
	// frames per second 
	pCodecCtxEnc->time_base= pCodecCtx->time_base;//(AVRational){1,25};
	pCodecCtxEnc->gop_size = 100; // emit one intra frame every ten frames 
	//pCodecCtxEnc->max_b_frames=1;
	pCodecCtxEnc->pix_fmt = PIX_FMT_YUV420P;
#endif

	fprintf(stderr, "INIT: VIDEO timebase OUT:%d %d IN: %d %d\n", pCodecCtxEnc->time_base.num, pCodecCtxEnc->time_base.den, pCodecCtx->time_base.num, pCodecCtx->time_base.den);

	// Find the decoder for the video stream
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

	if(audioStream!=-1) {
		//setup audio output encoder
		aCodecCtxEnc = avcodec_alloc_context();
		aCodecCtxEnc->bit_rate = audio_bitrate; //256000
		aCodecCtxEnc->sample_fmt = SAMPLE_FMT_S16;
		aCodecCtxEnc->sample_rate = aCodecCtx->sample_rate;
		aCodecCtxEnc->channels = aCodecCtx->channels;
		fprintf(stderr, "INIT: AUDIO bitrate OUT:%d sample_rate:%d channels:%d\n", aCodecCtxEnc->bit_rate, aCodecCtxEnc->sample_rate, aCodecCtxEnc->channels);

		// Find the decoder for the audio stream
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
	else {
		fprintf(stderr,"INIT: NO AUDIO TRACK IN INPUT FILE\n");
	}

	// Allocate audio in and out buffers
	samples = (int16_t *)av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
	if(samples == NULL) {
		fprintf(stderr, "INIT: Memory error alloc audio samples!!!\n");
		return -1;
	}
	audio_outbuf_size = STREAMER_MAX_AUDIO_BUFFER_SIZE;
	audio_outbuf = av_malloc(audio_outbuf_size);
	if(audio_outbuf == NULL) {
		fprintf(stderr, "INIT: Memory error alloc audio_outbuf!!!\n");
		return -1;
	}

	// Allocate video in frame and out buffer
	pFrame=avcodec_alloc_frame();
	if(pFrame==NULL) {
		fprintf(stderr, "INIT: Memory error alloc video frame!!!\n");
		return -1;
	}
	video_outbuf_size = STREAMER_MAX_VIDEO_BUFFER_SIZE;
	video_outbuf = av_malloc(video_outbuf_size);
	if(!video_outbuf) {
		fprintf(stderr, "INIT: Memory error alloc video_outbuf!!!\n");
		return -1;
	}

	//allocate Napa-Wine transport
	frame = (Frame *)malloc(sizeof(Frame));
	if(!frame) {
		fprintf(stderr, "INIT: Memory error alloc Frame!!!\n");
		return -1;
	}
	//create an empty first video chunk
	chunk = (ExternalChunk *)malloc(sizeof(ExternalChunk));
	if(!chunk) {
		fprintf(stderr, "INIT: Memory error alloc chunk!!!\n");
		return -1;
	}
	chunk->data = NULL;
	chunk->seq = 0;
	//initChunk(chunk, &seq_current_chunk); if i init them now i get out of sequence
#ifdef DEBUG_CHUNKER
	fprintf(stderr, "INIT: chunk video %d\n", chunk->seq);
#endif
	//create empty first audio chunk
	chunkaudio = (ExternalChunk *)malloc(sizeof(ExternalChunk));
	if(!chunkaudio) {
		fprintf(stderr, "INIT: Memory error alloc chunkaudio!!!\n");
		return -1;
	}
  chunkaudio->data=NULL;
	chunkaudio->seq = 0;
	//initChunk(chunkaudio, &seq_current_chunk);
#ifdef DEBUG_CHUNKER
	fprintf(stderr, "INIT: chunk audio %d\n", chunkaudio->seq);
#endif

	/* initialize the HTTP chunk pusher */
	initChunkPusher(); //TRIPLO

	long sleep=0;
	struct timeval now_tv;
	struct timeval tmp_tv;
	long long lateTime = 0;
	long long maxAudioInterval = 0;
	long long maxVDecodeTime = 0;

	//main loop to read from the input file
	while(av_read_frame(pFormatCtx, &packet)>=0)
	{
		//detect if a strange number of anomalies is occurring
		if(ptsvideo1 < 0 || ptsvideo1 > packet.dts || ptsaudio1 < 0 || ptsaudio1 > packet.dts) {
			pts_anomalies_counter++;
#ifdef DEBUG_ANOMALIES
			fprintf(stderr, "READLOOP: pts BASE anomaly detected number %d\n", pts_anomalies_counter);
#endif
			if(live_source) { //reset just in case of live source
				if(pts_anomalies_counter > 25) { //just a random threshold
					pts_anomalies_counter = 0;
					FirstTimeVideo = 1;
					FirstTimeAudio = 1;
#ifdef DEBUG_ANOMALIES
					fprintf(stderr, "READLOOP: too many pts BASE anomalies. resetting pts base\n");
#endif
				}
			}
		}

		if(newtime_anomalies_counter > 50) { //just a random threshold
			if(live_source) { //restart just in case of live source
#ifdef DEBUG_ANOMALIES
				fprintf(stderr, "READLOOP: too many NEGATIVE TIMESTAMPS anomalies. Restarting.\n");
#endif
				goto close;
			}
		}

		// Is this a packet from the video stream?
		if(packet.stream_index==videoStream)
		{
			if(!live_source)
			{
			if(audioStream != -1) { //take this "time bank" method into account only if we have audio track
				// lateTime < 0 means a positive time account that can be used to decode video frames
				// if (lateTime + maxVDecodeTime) >= 0 then we may have a negative time account after video transcoding
				// therefore, it's better to skip the frame
				if((lateTime+maxVDecodeTime) >= 0)
				{
#ifdef DEBUG_ANOMALIES
					fprintf(stderr, "\n\n\t\t************************* SKIPPING VIDEO FRAME ***********************************\n\n", sleep);
#endif
					continue;
				}
			}
			}
			
			gettimeofday(&tmp_tv, NULL);
			
			//decode the video packet into a raw pFrame
			if(avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet)>0)
			{
				// usleep(5000);
#ifdef DEBUG_VIDEO_FRAMES
				fprintf(stderr, "\n-------VIDEO FRAME type %d\n", pFrame->pict_type);
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
							last_pkt_dts = packet.dts;
						}
						else if(delta_video==0)
							//a Dts with a noPts value is troublesome case for delta calculation based on Dts
							continue;
					}
#ifdef DEBUG_VIDEO_FRAMES
					fprintf(stderr, "VIDEO: deltavideo : %d\n", (int)delta_video);
#endif
					video_frame_size = avcodec_encode_video(pCodecCtxEnc, video_outbuf, video_outbuf_size, pFrame);
					if(video_frame_size <= 0)
						continue;
#ifdef DEBUG_VIDEO_FRAMES
					fprintf(stderr, "VIDEO: original codec frame number %d vs. encoded %d vs. packed %d\n", pCodecCtx->frame_number, pCodecCtxEnc->frame_number, frame->number);
					fprintf(stderr, "VIDEO: duration %d timebase %d %d container timebase %d\n", (int)packet.duration, pCodecCtxEnc->time_base.den, pCodecCtxEnc->time_base.num, pCodecCtx->time_base.den);
#endif

					//use pts if dts is invalid
					if(packet.dts!=AV_NOPTS_VALUE)
						target_pts = packet.dts;
					else if(packet.pts!=AV_NOPTS_VALUE)
						target_pts = packet.pts;
					else
						continue;

					if(!offset_av)
					{
						if(FirstTimeVideo && packet.dts>0) {
							ptsvideo1 = (double)packet.dts;
							FirstTimeVideo = 0;
#ifdef DEBUG_VIDEO_FRAMES
							fprintf(stderr, "VIDEO: SET PTS BASE OFFSET %f\n", ptsvideo1);
#endif
						}
					}
					else //we want to compensate audio and video offset for this source
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
					}
					//compute the new video timestamp in milliseconds
					if(frame->number>0) {
						newTime = ((double)target_pts-ptsvideo1)*1000.0/((double)delta_video*(double)av_q2d(pFormatCtx->streams[videoStream]->r_frame_rate));
						// store timestamp in useconds for next frame sleep
						newTime_video = newTime*1000;
					}
#ifdef DEBUG_TIMESTAMPING
					fprintf(stderr, "VIDEO: NEWTIMESTAMP %ld\n", newTime);
#endif
					if(newTime<0) {
#ifdef DEBUG_VIDEO_FRAMES
						fprintf(stderr, "VIDEO: SKIPPING FRAME\n");
#endif
						newtime_anomalies_counter++;
						continue; //SKIP THIS FRAME, bad timestamp
					}
	
					frame->timestamp.tv_sec = (long long)newTime/1000;
					frame->timestamp.tv_usec = newTime%1000;
					frame->size = video_frame_size;
					/* pict_type maybe 1 (I), 2 (P), 3 (B), 5 (AUDIO)*/
					frame->type = pFrame->pict_type;
#ifdef DEBUG_VIDEO_FRAMES
					fprintf(stderr, "VIDEO: encapsulated frame size:%d type:%d\n", frame->size, frame->type);
					fprintf(stderr, "VIDEO: timestamped sec %d usec:%d\n", frame->timestamp.tv_sec, frame->timestamp.tv_usec);
#endif
					contFrameVideo++; //lets increase the numbering of the frames

					if(update_chunk(chunk, frame, video_outbuf) == -1) {
						fprintf(stderr, "VIDEO: unable to update chunk %d. Exiting.\n", chunk->seq);
						exit(-1);
					}

					if(chunkFilled(chunk, cmeta)) { // is chunk filled using current strategy?
						//SAVE ON FILE
						//saveChunkOnFile(chunk);
						//Send the chunk via http to an external transport/player
						pushChunkHttp(chunk, cmeta->outside_world_url);
#ifdef DEBUG_CHUNKER
						fprintf(stderr, "VIDEO: sent chunk video %d\n", chunk->seq);
#endif
						chunk->seq = 0; //signal that we need an increase
						//initChunk(chunk, &seq_current_chunk);
					}

					//compute how long it took to encode video frame
					gettimeofday(&now_tv, NULL);
					long long usec = (now_tv.tv_sec-tmp_tv.tv_sec)*1000000;
					usec+=(now_tv.tv_usec-tmp_tv.tv_usec);
					if(usec > maxVDecodeTime)
						maxVDecodeTime = usec;

					//we DONT have an audio track, so we compute timings and determine
					//how much time we have to sleep at next VIDEO frame taking
					//also into account how much time was needed to encode the current
					//video frame
					//all this in case the video source is not live, i.e. not self-timing
					//and only in case there is no audio track
					if(audioStream == -1) {
						if(!live_source) {
							if(newTime_prev != 0) {
								//how much delay between video frames ideally
								long long maxDelay = newTime_video - newTime_prev;
								sleep = (maxDelay - usec);
#ifdef DEBUG_ANOMALIES
								printf("\tmaxDelay=%ld\n", ((long)maxDelay));
								printf("\tlast video frame interval=%ld; sleep time=%ld\n", ((long)usec), ((long)sleep));
#endif
							}
							else
								sleep = 0;

							//update and store counters
							newTime_prev = newTime_video;

							//i can also sleep now instead of at the beginning of
							//the next frame because in this case we only have video
							//frames, hence it would immediately be the next thing to do
							if(sleep > 0) {
#ifdef DEBUG_ANOMALIES
								fprintf(stderr, "\n\tREADLOOP: going to sleep for %ld microseconds\n", sleep);
#endif
								usleep(sleep);
							}

						}
					}

				}
			}
		}
		else if(packet.stream_index==audioStream)
		{
			if(sleep > 0)
			{
#ifdef DEBUG_ANOMALIES
				fprintf(stderr, "\n\tREADLOOP: going to sleep for %ld microseconds\n", sleep);
#endif
				usleep(sleep);
			}
			
			audio_data_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
			//decode the audio packet into a raw audio source buffer
			if(avcodec_decode_audio3(aCodecCtx, samples, &audio_data_size, &packet)>0)
			{
#ifdef DEBUG_AUDIO_FRAMES
				fprintf(stderr, "\n-------AUDIO FRAME\n");
				fprintf(stderr, "AUDIO: newTimeaudioSTART : %lf\n", (double)(packet.pts)*av_q2d(pFormatCtx->streams[audioStream]->time_base));
#endif
				if(audio_data_size>0) {
#ifdef DEBUG_AUDIO_FRAMES
					fprintf(stderr, "AUDIO: datasizeaudio:%d\n", audio_data_size);
#endif
					/* if a frame has been decoded, output it */
					//fwrite(samples, 1, audio_data_size, outfileaudio);
				}
				else
					continue;
	
				audio_frame_size = avcodec_encode_audio(aCodecCtxEnc, audio_outbuf, audio_data_size, samples);
				if(audio_frame_size <= 0)
					continue;
				
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
						last_pkt_dts_audio = packet.dts;
					}
					else if(delta_audio==0)
						continue;
				}
#ifdef DEBUG_AUDIO_FRAMES
				fprintf(stderr, "AUDIO: original codec frame number %d vs. encoded %d vs. packed %d\n", aCodecCtx->frame_number, aCodecCtxEnc->frame_number, frame->number);
#endif
				//use pts if dts is invalid
				if(packet.dts!=AV_NOPTS_VALUE)
					target_pts = packet.dts;
				else if(packet.pts!=AV_NOPTS_VALUE)
					target_pts = packet.pts;
				else
					continue;

				if(!offset_av)
				{
					if(FirstTimeAudio && packet.dts>0) {
						ptsaudio1 = (double)packet.dts;
						FirstTimeAudio = 0;
#ifdef DEBUG_AUDIO_FRAMES
						fprintf(stderr, "AUDIO: SET PTS BASE OFFSET %f\n", ptsaudio1);
#endif
					}
				}
				else //we want to compensate audio and video offset for this source
				{
					if(FirstTimeAudio && packet.dts>0) {
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
				}
				//compute the new audio timestamps in milliseconds
				if(frame->number>0) {
					newTime = (((double)target_pts-ptsaudio1)*1000.0*((double)av_q2d(pFormatCtx->streams[audioStream]->time_base)));//*(double)delta_audio;
					// store timestamp in useconds for next frame sleep
					newTime_audio = newTime*1000;
				}
#ifdef DEBUG_TIMESTAMPING
				fprintf(stderr, "AUDIO: NEWTIMESTAMP %d\n", newTime);
#endif
				if(newTime<0) {
#ifdef DEBUG_AUDIO_FRAMES
					fprintf(stderr, "AUDIO: SKIPPING FRAME\n");
#endif
					newtime_anomalies_counter++;
					continue; //SKIP THIS FRAME, bad timestamp
				}

				frame->timestamp.tv_sec = (unsigned int)newTime/1000;
				frame->timestamp.tv_usec = newTime%1000;
				frame->size = audio_frame_size;
				frame->type = 5; // 5 is audio type
#ifdef DEBUG_AUDIO_FRAMES
				fprintf(stderr, "AUDIO: pts %d duration %d timebase %d %d dts %d\n", (int)packet.pts, (int)packet.duration, pFormatCtx->streams[audioStream]->time_base.num, pFormatCtx->streams[audioStream]->time_base.den, (int)packet.dts);
				fprintf(stderr, "AUDIO: timestamp sec:%d usec:%d\n", frame->timestamp.tv_sec, frame->timestamp.tv_usec);
				fprintf(stderr, "AUDIO: deltaaudio %lld\n", delta_audio);	
#endif
				contFrameAudio++;

				if(update_chunk(chunkaudio, frame, audio_outbuf) == -1) {
					fprintf(stderr, "AUDIO: unable to update chunk %d. Exiting.\n", chunkaudio->seq);
					exit(-1);
				}
				//set priority
				chunkaudio->priority = 1;

				if(chunkFilled(chunkaudio, cmeta)) {
					// is chunk filled using current strategy?
					//SAVE ON FILE
					//saveChunkOnFile(chunkaudio);
					//Send the chunk via http to an external transport/player
					pushChunkHttp(chunkaudio, cmeta->outside_world_url);
#ifdef DEBUG_CHUNKER
					fprintf(stderr, "AUDIO: just sent chunk audio %d\n", chunkaudio->seq);
#endif
					chunkaudio->seq = 0; //signal that we need an increase
					//initChunk(chunkaudio, &seq_current_chunk);
				}

				//we have an audio track, so we compute timings and determine
				//how much time we have to sleep at next audio frame taking
				//also into account how much time was needed to encode the
				//video frames
				//all this in case the video source is not live, i.e. not self-timing
				if(!live_source)
				{
					if(newTime_prev != 0)
					{
						long long maxDelay = newTime_audio - newTime_prev;

						gettimeofday(&now_tv, NULL);
						long long usec = (now_tv.tv_sec-lastAudioSent.tv_sec)*1000000;
						usec+=(now_tv.tv_usec-lastAudioSent.tv_usec);

						if(usec > maxAudioInterval)
							maxAudioInterval = usec;

						lateTime -= (maxDelay - usec);
#ifdef DEBUG_ANOMALIES
						printf("\tmaxDelay=%ld, maxAudioInterval=%ld\n", ((long)maxDelay), ((long) maxAudioInterval));
						printf("\tlast audio frame interval=%ld; lateTime=%ld\n", ((long)usec), ((long)lateTime));
#endif

						if((lateTime+maxAudioInterval) < 0)
							sleep = (lateTime+maxAudioInterval)*-1;
						else
							sleep = 0;
					}
					else
						sleep = 0;

					newTime_prev = newTime_audio;
					gettimeofday(&lastAudioSent, NULL);
				}

			}
		}
		else {
#ifdef DEBUG_CHUNKER
			fprintf(stderr,"Free the packet that was allocated by av_read_frame\n");
#endif
			av_free_packet(&packet);
		}
	}

close:
	if(chunk->seq != 0 && chunk->frames_num>0) {
		//SAVE ON FILE
		//saveChunkOnFile(chunk);
		//Send the chunk via http to an external transport/player
		pushChunkHttp(chunk, cmeta->outside_world_url);
#ifdef DEBUG_CHUNKER
		fprintf(stderr, "CHUNKER: SENDING LAST VIDEO CHUNK\n");
#endif
		chunk->seq = 0; //signal that we need an increase just in case we will restart
	}
	if(chunkaudio->seq != 0 && chunkaudio->frames_num>0) {
		//SAVE ON FILE     
		//saveChunkOnFile(chunkaudio);
		//Send the chunk via http to an external transport/player
		pushChunkHttp(chunkaudio, cmeta->outside_world_url);
#ifdef DEBUG_CHUNKER
		fprintf(stderr, "CHUNKER: SENDING LAST AUDIO CHUNK\n");
#endif
		chunkaudio->seq = 0; //signal that we need an increase just in case we will restart
	}

	/* finalize the HTTP chunk pusher */
	finalizeChunkPusher();

	free(chunk);
	free(chunkaudio);
	free(frame);
	av_free(video_outbuf);
	av_free(audio_outbuf);
	free(cmeta);

	// Free the YUV frame
	av_free(pFrame);
	av_free(samples);
  
	// Close the codec
	avcodec_close(pCodecCtx);
	avcodec_close(pCodecCtxEnc);

	if(audioStream!=-1) {
		avcodec_close(aCodecCtx);
		avcodec_close(aCodecCtxEnc);
	}
  
	// Close the video file
	av_close_input_file(pFormatCtx);

	if(LOOP_MODE) {
		//we want video to continue, but the av_read_frame stopped
		//lets wait a 5 secs, and cycle in again
		usleep(5000000);
#ifdef DEBUG_CHUNKER
		fprintf(stderr, "CHUNKER: WAITING 5 secs FOR LIVE SOURCE TO SKIP ERRORS AND RESTARTING\n");
#endif
		videoStream = -1;
		audioStream = -1;
		FirstTimeAudio=1;
		FirstTimeVideo=1;
		pts_anomalies_counter=0;
		newtime_anomalies_counter=0;
		newTime=0;
		newTime_audio=0;
		newTime_prev=0;
		ptsvideo1=0.0;
		ptsaudio1=0.0;
		last_pkt_dts=0;
		delta_video=0;
		delta_audio=0;
		last_pkt_dts_audio=0;
		target_pts=0;
		i=0;
		goto restart;
	}

	return 0;
}


int update_chunk(ExternalChunk *chunk, Frame *frame, uint8_t *outbuf) {
	//the frame.h gets encoded into 5 slots of 32bits (3 ints plus 2 more for the timeval struct
	static int sizeFrameHeader = 5*sizeof(int32_t);

	//moving temp pointer to encode Frame on the wire
	uint8_t *tempdata = NULL;

	if(chunk->seq == 0) {
		initChunk(chunk, &seq_current_chunk);
	}
	//HINT on malloc
	chunk->data = (uint8_t *)realloc(chunk->data, sizeof(uint8_t)*(chunk->payload_len + frame->size + sizeFrameHeader));
	if(!chunk->data)  {
		fprintf(stderr, "Memory error in chunk!!!\n");
		return -1;
	}
	chunk->frames_num++; // number of frames in the current chunk

/*
	//package the Frame header
	tempdata = chunk->data+chunk->payload_len;
	*((int32_t *)tempdata) = frame->number;
	tempdata+=sizeof(int32_t);
	*((struct timeval *)tempdata) = frame->timestamp;
	tempdata+=sizeof(struct timeval);
	*((int32_t *)tempdata) = frame->size;
	tempdata+=sizeof(int32_t);
	*((int32_t *)tempdata) = frame->type;
	tempdata+=sizeof(int32_t);
*/
	//package the Frame header: network order and platform independent
	tempdata = chunk->data+chunk->payload_len;
	bit32_encoded_push(frame->number, tempdata);
	bit32_encoded_push(frame->timestamp.tv_sec, tempdata + CHUNK_TRANSCODING_INT_SIZE);
	bit32_encoded_push(frame->timestamp.tv_usec, tempdata + CHUNK_TRANSCODING_INT_SIZE*2);
	bit32_encoded_push(frame->size, tempdata + CHUNK_TRANSCODING_INT_SIZE*3);
	bit32_encoded_push(frame->type, tempdata + CHUNK_TRANSCODING_INT_SIZE*4);

	 //insert the new frame data
	memcpy(chunk->data + chunk->payload_len + sizeFrameHeader, outbuf, frame->size);
	chunk->payload_len += frame->size + sizeFrameHeader; // update payload length
	//chunk lenght is updated just prior to pushing it out because
	//the chunk header len is better calculated there
	//chunk->len = sizeChunkHeader + chunk->payload_len; // update overall length

	//update timestamps
	if(((int)frame->timestamp.tv_sec < (int)chunk->start_time.tv_sec) || ((int)frame->timestamp.tv_sec==(int)chunk->start_time.tv_sec && (int)frame->timestamp.tv_usec < (int)chunk->start_time.tv_usec) || (int)chunk->start_time.tv_sec==-1) {
						chunk->start_time.tv_sec = frame->timestamp.tv_sec;
						chunk->start_time.tv_usec = frame->timestamp.tv_usec;
	}
	
	if(((int)frame->timestamp.tv_sec > (int)chunk->end_time.tv_sec) || ((int)frame->timestamp.tv_sec==(int)chunk->end_time.tv_sec && (int)frame->timestamp.tv_usec > (int)chunk->end_time.tv_usec) || (int)chunk->end_time.tv_sec==-1) {
						chunk->end_time.tv_sec = frame->timestamp.tv_sec;
						chunk->end_time.tv_usec = frame->timestamp.tv_usec;
	}
	return 0;
}

