/*
 *  Copyright (c) 2009-2011 Carmelo Daniele, Dario Marchese, Diego Reforgiato, Giuseppe Tropea
 *  Copyright (c) 2010-2011 Csaba Kiraly
 *  developed for the Napa-Wine EU project. See www.napa-wine.eu
 *
 *  This is free software; see lgpl-2.1.txt
 */

#include "chunker_streamer.h"
#include <signal.h>
#include <math.h>
#include <getopt.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>

#ifdef USE_AVFILTER
#include <libavfilter/avfilter.h>
#include "chunker_filtering.h"
#endif

#include "chunk_pusher.h"

struct outstream {
	struct output *output;
	ExternalChunk *chunk;
	AVCodecContext *pCodecCtxEnc;
};
#define QUALITYLEVELS_MAX 9
struct outstream outstream[1+QUALITYLEVELS_MAX+1];
int qualitylevels = 3;
int indexchannel = 1;

#define DEBUG
#define DEBUG_AUDIO_FRAMES  false
#define DEBUG_VIDEO_FRAMES  false
#define DEBUG_CHUNKER false
#define DEBUG_ANOMALIES true
#define DEBUG_TIMESTAMPING false
#include "dbg.h"

#define STREAMER_MAX(a,b) ((a>b)?(a):(b))
#define STREAMER_MIN(a,b) ((a<b)?(a):(b))

//#define DISPLAY_PSNR
#define GET_PSNR(x) ((x==0) ? 0 : (-10.0*log(x)/log(10)))

ChunkerMetadata *cmeta = NULL;
int seq_current_chunk = 1; //chunk numbering starts from 1; HINT do i need more bytes?

#define AUDIO_CHUNK 0
#define VIDEO_CHUNK 1

void SaveFrame(AVFrame *pFrame, int width, int height);
void SaveEncodedFrame(Frame* frame, uint8_t *video_outbuf);
int update_chunk(ExternalChunk *chunk, Frame *frame, uint8_t *outbuf);
void bit32_encoded_push(uint32_t v, uint8_t *p);

int video_record_count = 0;
int savedVideoFrames = 0;
long int firstSavedVideoFrame = 0;
int ChunkerStreamerTestMode = 0;

int pts_anomaly_threshold = -1;
int newtime_anomaly_threshold = -1;
bool timebank = false;
char *outside_world_url = NULL;

int gop_size = 25;
int max_b_frames = 3;
bool vcopy = false;

long delay_audio = 0; //delay audio by x millisec

char *avfilter="yadif";

// Constant number of frames per chunk
int chunkFilledFramesStrategy(ExternalChunk *echunk, int chunkType)
{
	dcprintf(DEBUG_CHUNKER, "CHUNKER: check if frames num %d == %d in chunk %d\n", echunk->frames_num, cmeta->framesPerChunk[chunkType], echunk->seq);
	if(echunk->frames_num == cmeta->framesPerChunk[chunkType])
		return 1;

	return 0;
}

// Constant size. Note that for now each chunk will have a size just greater or equal than the required value
// It can be considered as constant size.
int chunkFilledSizeStrategy(ExternalChunk *echunk, int chunkType)
{
	dcprintf(DEBUG_CHUNKER, "CHUNKER: check if chunk size %d >= %d in chunk %d\n", echunk->payload_len, cmeta->targetChunkSize, echunk->seq);
	if(echunk->payload_len >= cmeta->targetChunkSize)
		return 1;
	
	return 0;
}

// Performace optimization.
// The chunkFilled function has been splitted into two functions (one for each strategy).
// Instead of continuously check the strategy flag (which is constant),
// we change the callback just once according to the current strategy (look at the switch statement in the main in which this function pointer is set)
int (*chunkFilled)(ExternalChunk *echunk, int chunkType);

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

int quit = 0;

void sigproc()
{
	printf("you have pressed ctrl-c, terminating...\n");
	quit = 1;
}

static void print_usage(int argc, char *argv[])
{
  fprintf (stderr,
    "\nUsage:%s [options]\n"
    "\n"
    "Mandatory options:\n"
    "\t[-i input file]\n"
    "\t[-a audio bitrate]\n"
    "\t[-v video bitrate]\n\n"
    "Other options:\n"
    "\t[-F output] (overrides config file)\n"
    "\t[-A audioencoder]\n"
    "\t[-V videoencoder]\n"
    "\t[-s WxH]: force video size.\n"
    "\t[-l]: this is a live stream.\n"
    "\t[-o]: adjust A/V frame timestamps (deafault off, use it only with flawed containers)\n"
    "\t[-p]: pts anomaly threshold (default: -1=off).\n"
    "\t[-q]: sync anomaly threshold ((default: -1=off).\n"
    "\t[-t]: QoE test mode\n\n"

    "\t[--video_stream]:set video_stream ID in input\n"
    "\t[--audio_stream]:set audio_stream ID in input\n"
    "\t[--avfilter]:set input filter (default: yadif\n"
    "\t[--no-indexchannel]: turn off generation of index channel\n"
    "\t[--qualitylevels]:set number of quality levels\n"
    "\n"
    "Codec options:\n"
    "\t[-g GOP]: gop size\n"
    "\t[-b frames]: max number of consecutive b frames\n"
    "\t[-x extas]: extra video codec options (e.g. -x me_method=hex,flags2=+dct8x8+wpred+bpyrami+mixed_refs)\n"
    "\n"
    "=======================================================\n", argv[0]
    );
  }

int sendChunk(struct output *output, ExternalChunk *chunk) {
#ifdef HTTPIO
						return pushChunkHttp(chunk, outside_world_url);
#endif
#ifdef TCPIO
						return pushChunkTcp(output, chunk);
#endif
#ifdef UDPIO
						return pushChunkUDP(chunk);
#endif
}

AVFrame *preprocessFrame(AVFrame *pFrame) {
#ifdef USE_AVFILTER
	AVFrame *pFrame2 = NULL;
	pFrame2=avcodec_alloc_frame();
	if(pFrame2==NULL) {
		fprintf(stderr, "INIT: Memory error alloc video frame!!!\n");
		if(pFrame2) av_free(pFrame2);
		return NULL;
	}
#endif

#ifdef VIDEO_DEINTERLACE
	avpicture_deinterlace(
		(AVPicture*) pFrame,
		(const AVPicture*) pFrame,
		pCodecCtxEnc->pix_fmt,
		pCodecCtxEnc->width,
		pCodecCtxEnc->height);
#endif

#ifdef USE_AVFILTER
	//apply avfilters
	filter(pFrame,pFrame2);
	dcprintf(DEBUG_VIDEO_FRAMES, "VIDEOfilter: pkt_dts %"PRId64" pkt_pts %"PRId64" frame.pts %"PRId64"\n", pFrame2->pkt_dts, pFrame2->pkt_pts, pFrame2->pts);
	dcprintf(DEBUG_VIDEO_FRAMES, "VIDEOfilter intype %d%s\n", pFrame2->pict_type, pFrame2->key_frame ? " (key)" : "");
	return pFrame2;
#else
	return NULL;
#endif
}


int transcodeFrame(uint8_t *video_outbuf, int video_outbuf_size, int64_t *target_pts, AVFrame *pFrame, AVRational time_base, AVCodecContext *pCodecCtx, AVCodecContext *pCodecCtxEnc)
{
	int video_frame_size = 0;
	AVFrame *scaledFrame = NULL;
	scaledFrame=avcodec_alloc_frame();
	if(scaledFrame==NULL) {
		fprintf(stderr, "INIT: Memory error alloc video frame!!!\n");
		if(scaledFrame) av_free(scaledFrame);
		return -1;
	}
	int scaledFrame_buf_size = avpicture_get_size( PIX_FMT_YUV420P, pCodecCtxEnc->width, pCodecCtxEnc->height);
	uint8_t* scaledFrame_buffer = (uint8_t *) av_malloc( scaledFrame_buf_size * sizeof( uint8_t ) );
	avpicture_fill( (AVPicture*) scaledFrame, scaledFrame_buffer, PIX_FMT_YUV420P, pCodecCtxEnc->width, pCodecCtxEnc->height);
	if(!video_outbuf || !scaledFrame_buffer) {
		fprintf(stderr, "INIT: Memory error alloc video_outbuf!!!\n");
		return -1;
	}



					    if(pCodecCtx->height != pCodecCtxEnc->height || pCodecCtx->width != pCodecCtxEnc->width) {
//						static AVPicture pict;
						static struct SwsContext *img_convert_ctx = NULL;

						pFrame->pict_type = 0;
						img_convert_ctx = sws_getCachedContext(img_convert_ctx, pCodecCtx->width, pCodecCtx->height, PIX_FMT_YUV420P, pCodecCtxEnc->width, pCodecCtxEnc->height, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
						if(img_convert_ctx == NULL) {
							fprintf(stderr, "Cannot initialize the conversion context!\n");
							exit(1);
						}
						sws_scale(img_convert_ctx, pFrame->data, pFrame->linesize, 0, pCodecCtx->height, scaledFrame->data, scaledFrame->linesize);
						scaledFrame->pts = pFrame->pts;
						scaledFrame->pict_type = 0;
						video_frame_size = avcodec_encode_video(pCodecCtxEnc, video_outbuf, video_outbuf_size, scaledFrame);
					    } else {
						pFrame->pict_type = 0;
						video_frame_size = avcodec_encode_video(pCodecCtxEnc, video_outbuf, video_outbuf_size, pFrame);
					    }

					    //use pts if dts is invalid
					    if(pCodecCtxEnc->coded_frame->pts!=AV_NOPTS_VALUE)
						*target_pts = av_rescale_q(pCodecCtxEnc->coded_frame->pts, pCodecCtxEnc->time_base, time_base);
					    else {	//TODO: review this
						if(scaledFrame) av_free(scaledFrame);
						if(scaledFrame_buffer) av_free(scaledFrame_buffer);
						return -1;
					    }

					if(video_frame_size > 0) {
					    if(pCodecCtxEnc->coded_frame) {
						dcprintf(DEBUG_VIDEO_FRAMES, "VIDEOout: pkt_dts %"PRId64" pkt_pts %"PRId64" frame.pts %"PRId64"\n", pCodecCtxEnc->coded_frame->pkt_dts, pCodecCtxEnc->coded_frame->pkt_pts, pCodecCtxEnc->coded_frame->pts);
						dcprintf(DEBUG_VIDEO_FRAMES, "VIDEOout: outtype: %d%s\n", pCodecCtxEnc->coded_frame->pict_type, pCodecCtxEnc->coded_frame->key_frame ? " (key)" : "");
					    }
#ifdef DISPLAY_PSNR
					    static double ist_psnr = 0;
					    static double cum_psnr = 0;
					    static int psnr_samples = 0;
					    if(pCodecCtxEnc->coded_frame) {
						if(pCodecCtxEnc->flags&CODEC_FLAG_PSNR) {
							ist_psnr = GET_PSNR(pCodecCtxEnc->coded_frame->error[0]/(pCodecCtxEnc->width*pCodecCtxEnc->height*255.0*255.0));
							psnr_samples++;
							cum_psnr += ist_psnr;
							fprintf(stderr, "PSNR: ist %.4f avg: %.4f\n", ist_psnr, cum_psnr / (double)psnr_samples);
						}
					    }
#endif
					}

	if(scaledFrame) av_free(scaledFrame);
	if(scaledFrame_buffer) av_free(scaledFrame_buffer);
	return video_frame_size;
}


void createFrame(struct Frame *frame, long long newTime, int video_frame_size, int pict_type)
{

					frame->timestamp.tv_sec = (long long)newTime/1000;
					frame->timestamp.tv_usec = newTime%1000;
					frame->size = video_frame_size;
					/* pict_type maybe 1 (I), 2 (P), 3 (B), 5 (AUDIO)*/
					frame->type = pict_type;


/* should be on some other place
//					if (!vcopy) dcprintf(DEBUG_VIDEO_FRAMES, "VIDEO: original codec frame number %d vs. encoded %d vs. packed %d\n", pCodecCtx->frame_number, pCodecCtxEnc->frame_number, frame->number);
//					if (!vcopy) dcprintf(DEBUG_VIDEO_FRAMES, "VIDEO: duration %d timebase %d %d container timebase %d\n", (int)packet.duration, pCodecCtxEnc->time_base.den, pCodecCtxEnc->time_base.num, pCodecCtx->time_base.den);

#ifdef YUV_RECORD_ENABLED
					if(!vcopy && ChunkerStreamerTestMode)
					{
						if(videotrace)
							fprintf(videotrace, "%d %d %d\n", frame->number, pict_type, frame->size);

						SaveFrame(pFrame, dest_width, dest_height);

						++savedVideoFrames;
						SaveEncodedFrame(frame, video_outbuf);

						if(!firstSavedVideoFrame)
							firstSavedVideoFrame = frame->number;

						char tmp_filename[255];
						sprintf(tmp_filename, "yuv_data/streamer_out_context.txt");
						FILE* tmp = fopen(tmp_filename, "w");
						if(tmp)
						{
							fprintf(tmp, "width = %d\nheight = %d\ntotal_frames_saved = %d\ntotal_frames_decoded = %d\nfirst_frame_number = %ld\nlast_frame_number = %d\n"
								,dest_width, dest_height
								,savedVideoFrames, savedVideoFrames, firstSavedVideoFrame, frame->number);
							fclose(tmp);
						}
					}
#endif
*/

					dcprintf(DEBUG_VIDEO_FRAMES, "VIDEO: encapsulated frame size:%d type:%d\n", frame->size, frame->type);
					dcprintf(DEBUG_VIDEO_FRAMES, "VIDEO: timestamped sec %ld usec:%ld\n", (long)frame->timestamp.tv_sec, (long)frame->timestamp.tv_usec);
}


void addFrameToOutstream(struct outstream *os, Frame *frame, uint8_t *video_outbuf)
{

	ExternalChunk *chunk = os->chunk;
	struct output *output = os->output;

					if(update_chunk(chunk, frame, video_outbuf) == -1) {
						fprintf(stderr, "VIDEO: unable to update chunk %d. Exiting.\n", chunk->seq);
						exit(-1);
					}

					if(chunkFilled(chunk, VIDEO_CHUNK)) { // is chunk filled using current strategy?
						//calculate priority
						chunk->priority /= chunk->frames_num;

						//SAVE ON FILE
						//saveChunkOnFile(chunk);
						//Send the chunk to an external transport/player
						sendChunk(output, chunk);
						dctprintf(DEBUG_CHUNKER, "VIDEO: sent chunk video %d, prio:%f, size %d\n", chunk->seq, chunk->priority, chunk->len);
						chunk->seq = 0; //signal that we need an increase
						//initChunk(chunk, &seq_current_chunk);
					}
}

long long pts2ms(int64_t pts, AVRational time_base)
{
	return pts * 1000 * time_base.num / time_base.den;
}

AVCodecContext *openVideoEncoder(const char *video_codec, int video_bitrate, int dest_width, int dest_height, AVRational time_base, const char *codec_options) {

	AVCodec *pCodecEnc;
	AVCodecContext *pCodecCtxEnc;

	//setup video output encoder
	if (strcmp(video_codec, "copy") == 0) {
		return NULL;
	}

	pCodecEnc = avcodec_find_encoder_by_name(video_codec);
	if (pCodecEnc) {
		fprintf(stderr, "INIT: Setting VIDEO codecID to: %d\n",pCodecEnc->id);
	} else {
		fprintf(stderr, "INIT: Unknown OUT VIDEO codec: %s!\n", video_codec);
		return NULL; // Codec not found
	}

	pCodecCtxEnc=avcodec_alloc_context();
	pCodecCtxEnc->codec_type = CODEC_TYPE_VIDEO;
	pCodecCtxEnc->codec_id = pCodecEnc->id;

	pCodecCtxEnc->bit_rate = video_bitrate;
	//~ pCodecCtxEnc->qmin = 30;
	//~ pCodecCtxEnc->qmax = 30;
	//times 20 follows the defaults, was not needed in previous versions of libavcodec
//	pCodecCtxEnc->crf = 20.0f;
	// resolution must be a multiple of two 
	pCodecCtxEnc->width = dest_width;
	pCodecCtxEnc->height = dest_height;
	// frames per second 
	//~ pCodecCtxEnc->time_base= pCodecCtx->time_base;//(AVRational){1,25};
	//printf("pCodecCtx->time_base=%d/%d\n", pCodecCtx->time_base.num, pCodecCtx->time_base.den);
	pCodecCtxEnc->time_base= time_base;//(AVRational){1,25};
	pCodecCtxEnc->gop_size = gop_size; // emit one intra frame every gop_size frames 
	pCodecCtxEnc->max_b_frames = max_b_frames;
	pCodecCtxEnc->pix_fmt = PIX_FMT_YUV420P;
	pCodecCtxEnc->flags |= CODEC_FLAG_PSNR;
	//~ pCodecCtxEnc->flags |= CODEC_FLAG_QSCALE;

	//some generic quality tuning
	pCodecCtxEnc->mb_decision = FF_MB_DECISION_RD;

	//some rate control parameters for streaming, taken from ffserver.c
	{
        /* Bitrate tolerance is less for streaming */
	AVCodecContext *av = pCodecCtxEnc;
        if (av->bit_rate_tolerance == 0)
            av->bit_rate_tolerance = FFMAX(av->bit_rate / 4,
                      (int64_t)av->bit_rate*av->time_base.num/av->time_base.den);
        //if (av->qmin == 0)
        //    av->qmin = 3;
        //if (av->qmax == 0)
        //    av->qmax = 31;
        //if (av->max_qdiff == 0)
        //    av->max_qdiff = 3;
        //av->qcompress = 0.5;
        //av->qblur = 0.5;

        //if (!av->nsse_weight)
        //    av->nsse_weight = 8;

        //av->frame_skip_cmp = FF_CMP_DCTMAX;
        //if (!av->me_method)
        //    av->me_method = ME_EPZS;
        //av->rc_buffer_aggressivity = 1.0;

        //if (!av->rc_eq)
        //    av->rc_eq = "tex^qComp";
        //if (!av->i_quant_factor)
        //    av->i_quant_factor = -0.8;
        //if (!av->b_quant_factor)
        //    av->b_quant_factor = 1.25;
        //if (!av->b_quant_offset)
        //    av->b_quant_offset = 1.25;
        if (!av->rc_max_rate)
            av->rc_max_rate = av->bit_rate * 2;

        if (av->rc_max_rate && !av->rc_buffer_size) {
            av->rc_buffer_size = av->rc_max_rate;
        }
	}
	//end of code taken fromffserver.c

  switch (pCodecEnc->id) {
    case CODEC_ID_H264 :
	// Fast Profile
	// libx264-fast.ffpreset preset 
	pCodecCtxEnc->coder_type = FF_CODER_TYPE_AC; // coder = 1 -> enable CABAC
	pCodecCtxEnc->flags |= CODEC_FLAG_LOOP_FILTER; // flags=+loop -> deblock
	pCodecCtxEnc->me_cmp|= 1; // cmp=+chroma, where CHROMA = 1
        pCodecCtxEnc->partitions |= X264_PART_I8X8|X264_PART_I4X4|X264_PART_P8X8|X264_PART_B8X8;	// partitions=+parti8x8+parti4x4+partp8x8+partb8x8
	pCodecCtxEnc->me_method=ME_HEX; // me_method=hex
	pCodecCtxEnc->me_subpel_quality = 6; // subq=7
	pCodecCtxEnc->me_range = 16; // me_range=16
	//pCodecCtxEnc->gop_size = 250; // g=250
	//pCodecCtxEnc->keyint_min = 25; // keyint_min=25
	pCodecCtxEnc->scenechange_threshold = 40; // sc_threshold=40
	pCodecCtxEnc->i_quant_factor = 0.71; // i_qfactor=0.71
	pCodecCtxEnc->b_frame_strategy = 1; // b_strategy=1
	pCodecCtxEnc->qcompress = 0.6; // qcomp=0.6
	pCodecCtxEnc->qmin = 10; // qmin=10
	pCodecCtxEnc->qmax = 51; // qmax=51
	pCodecCtxEnc->max_qdiff = 4; // qdiff=4
	//pCodecCtxEnc->max_b_frames = 3; // bf=3
	pCodecCtxEnc->refs = 2; // refs=3
	//pCodecCtxEnc->directpred = 1; // directpred=1
	pCodecCtxEnc->directpred = 3; // directpred=1 in preset -> "directpred", "direct mv prediction mode - 0 (none), 1 (spatial), 2 (temporal), 3 (auto)"
	//pCodecCtxEnc->trellis = 1; // trellis=1
	pCodecCtxEnc->flags2 |= CODEC_FLAG2_BPYRAMID|CODEC_FLAG2_MIXED_REFS|CODEC_FLAG2_WPRED|CODEC_FLAG2_8X8DCT|CODEC_FLAG2_FASTPSKIP;	// flags2=+bpyramid+mixed_refs+wpred+dct8x8+fastpskip
	pCodecCtxEnc->weighted_p_pred = 2; // wpredp=2

	// libx264-main.ffpreset preset
	//pCodecCtxEnc->flags2|=CODEC_FLAG2_8X8DCT;
	//pCodecCtxEnc->flags2^=CODEC_FLAG2_8X8DCT; // flags2=-dct8x8
	//pCodecCtxEnc->crf = 22;

#ifdef STREAMER_X264_USE_SSIM
	pCodecCtxEnc->flags2 |= CODEC_FLAG2_SSIM;
#endif

	//pCodecCtxEnc->weighted_p_pred=2; //maps wpredp=2; weighted prediction analysis method
	// pCodecCtxEnc->rc_min_rate = 0;
	// pCodecCtxEnc->rc_max_rate = video_bitrate*2;
	// pCodecCtxEnc->rc_buffer_size = 0;
	break;
    case CODEC_ID_MPEG4 :
	break;
    default:
	fprintf(stderr, "INIT: Unsupported OUT VIDEO codec: %s!\n", video_codec);
  }

  if ((av_set_options_string(pCodecCtxEnc, codec_options, "=", ",")) < 0) {
    fprintf(stderr, "Error parsing options string: '%s'\n", codec_options);
    return NULL;
  }

  if(avcodec_open(pCodecCtxEnc, pCodecEnc)<0) {
    fprintf(stderr, "INIT: could not open OUT VIDEO codecEnc\n");
    return NULL; // Could not open codec
  }

 return pCodecCtxEnc;
}


int main(int argc, char *argv[]) {
	signal(SIGINT, sigproc);
	
	int i=0,j,k;

	//output variables
	uint8_t *video_outbuf = NULL;
	int video_outbuf_size, video_frame_size;
	uint8_t *audio_outbuf = NULL;
	int audio_outbuf_size, audio_frame_size;
	int audio_data_size;

	//numeric identifiers of input streams
	int videoStream = -1;
	int audioStream = -1;

//	int len1;
	int frameFinished;
	//frame sequential counters
	int contFrameAudio=1, contFrameVideo=0;
//	int numBytes;

	//command line parameters
	int audio_bitrate = -1;
	int video_bitrate = -1;
	char *audio_codec = "mp2";
	char *video_codec = "mpeg4";
	char *codec_options = "";
	int live_source = 0; //tells to sleep before reading next frame in not live (i.e. file)
	int offset_av = 0; //tells to compensate for offset between audio and video in the file
	
	//a raw buffer for decoded uncompressed audio samples
	int16_t *samples = NULL;
	//a raw uncompressed video picture
	AVFrame *pFrame1 = NULL;

	AVFormatContext *pFormatCtx;
	AVCodecContext  *pCodecCtx = NULL ,*aCodecCtxEnc = NULL ,*aCodecCtx = NULL;
	AVCodec         *pCodec = NULL ,*aCodec = NULL ,*aCodecEnc = NULL;
	AVPacket         packet;

	//stuff needed to compute the right timestamps
	short int FirstTimeAudio=1, FirstTimeVideo=1;
	short int pts_anomalies_counter=0;
	short int newtime_anomalies_counter=0;
	long long newTime=0, newTime_audio=0, newTime_video=0, newTime_prev=0;
	struct timeval lastAudioSent = {0, 0};
	int64_t ptsvideo1=0;
	int64_t ptsaudio1=0;
	int64_t last_pkt_dts=0, delta_video=0, delta_audio=0, last_pkt_dts_audio=0, target_pts=0;

	//Napa-Wine specific Frame and Chunk structures for transport
	Frame *frame = NULL;
	ExternalChunk *chunkaudio = NULL;
	
	char av_input[1024];
	int dest_width = -1;
	int dest_height = -1;
	
	static struct option long_options[] =
	{
		{"audio_stream", required_argument, 0, 0},
		{"video_stream", required_argument, 0, 0},
		{"avfilter", required_argument, 0, 0},
		{"indexchannel", no_argument, &indexchannel, 1},
		{"no-indexchannel", no_argument, &indexchannel, 0},
		{"qualitylevels", required_argument, 0, 'Q'},
		{0, 0, 0, 0}
	};
	/* `getopt_long' stores the option index here. */
	int option_index = 0, c;
	int mandatories = 0;
	while ((c = getopt_long (argc, argv, "i:a:v:A:V:s:lop:q:tF:g:b:d:x:Q:", long_options, &option_index)) != -1)
	{
		switch (c) {
			case 0: //for long options
				if( strcmp( "audio_stream", long_options[option_index].name ) == 0 ) { audioStream = atoi(optarg); }
				if( strcmp( "video_stream", long_options[option_index].name ) == 0 ) { videoStream = atoi(optarg); }
				if( strcmp( "avfilter", long_options[option_index].name ) == 0 ) { avfilter = strdup(optarg); }
				break;
			case 'i':
				sprintf(av_input, "%s", optarg);
				mandatories++;
				break;
			case 'a':
				sscanf(optarg, "%d", &audio_bitrate);
				mandatories++;
				break;
			case 'v':
				sscanf(optarg, "%d", &video_bitrate);
				mandatories++;
				break;
			case 'A':
				audio_codec = strdup(optarg);
				break;
			case 'V':
				video_codec = strdup(optarg);
				break;
			case 's':
				sscanf(optarg, "%dx%d", &dest_width, &dest_height);
				break;
			case 'l':
				live_source = 1;
				break;
			case 'o':
				offset_av = 1;
				break;
			case 't':
				ChunkerStreamerTestMode = 1;
				break;
			case 'p':
				sscanf(optarg, "%d", &pts_anomaly_threshold);
				break;
			case 'q':
				sscanf(optarg, "%d", &newtime_anomaly_threshold);
				break;
			case 'F':
				outside_world_url = strdup(optarg);
				break;
			case 'g':
				sscanf(optarg, "%d", &gop_size);
				break;
			case 'b':
				sscanf(optarg, "%d", &max_b_frames);
				break;
			case 'd':
				sscanf(optarg, "%ld", &delay_audio);
				break;
			case 'x':
				codec_options = strdup(optarg);
				break;
			case 'Q':
				sscanf(optarg, "%d", &qualitylevels);
				if (qualitylevels > QUALITYLEVELS_MAX) {
					fprintf(stderr,"Too many quality levels: %d (max:%d)\n", qualitylevels, QUALITYLEVELS_MAX);
					return -1;
				}
				break;
			default:
				print_usage(argc, argv);
				return -1;
		}
	}
	
	if(mandatories < 3) 
	{
		print_usage(argc, argv);
		return -1;
	}

#ifdef YUV_RECORD_ENABLED
	if(ChunkerStreamerTestMode)
	{
		DELETE_DIR("yuv_data");
		CREATE_DIR("yuv_data");
		//FILE* pFile=fopen("yuv_data/streamer_out.yuv", "w");
		//fclose(pFile);
	}
#endif

#ifdef TCPIO
	static char peer_ip[16];
	static int peer_port;
	int res = sscanf(outside_world_url, "tcp://%15[0-9.]:%d", peer_ip, &peer_port);
	if (res < 2) {
		fprintf(stderr,"error parsing output url: %s\n", outside_world_url);
		return -2;
	}

	for (i=0; i < 1 + qualitylevels + (indexchannel?1:0); i++) {
		outstream[i].output = initTCPPush(peer_ip, peer_port+i);
		if (!outstream[i].output) {
			fprintf(stderr, "Error initializing output module, exiting\n");
			exit(1);
		}
	}
#endif

restart:
	// read the configuration file
	cmeta = chunkerInit();
	if (!outside_world_url) {
		outside_world_url = strdup(cmeta->outside_world_url);
	}
	switch(cmeta->strategy)
	{
		case 1:
			chunkFilled = chunkFilledSizeStrategy;
			break;
		default:
			chunkFilled = chunkFilledFramesStrategy;
	}
		
	if(live_source)
		fprintf(stderr, "INIT: Using LIVE SOURCE TimeStamps\n");
	if(offset_av)
		fprintf(stderr, "INIT: Compensating AV OFFSET in file\n");

	// Register all formats and codecs
	av_register_all();

	// Open input file
	if(av_open_input_file(&pFormatCtx, av_input, NULL, 0, NULL) != 0) {
		fprintf(stdout, "INIT: Couldn't open video file. Exiting.\n");
		exit(-1);
	}

	// Retrieve stream information
	if(av_find_stream_info(pFormatCtx) < 0) {
		fprintf(stdout, "INIT: Couldn't find stream information. Exiting.\n");
		exit(-1);
	}

	// Dump information about file onto standard error
	av_dump_format(pFormatCtx, 0, av_input, 0);

	// Find the video and audio stream numbers
	for(i=0; i<pFormatCtx->nb_streams; i++) {
		if(pFormatCtx->streams[i]->codec->codec_type==CODEC_TYPE_VIDEO && videoStream<0) {
			videoStream=i;
		}
		if(pFormatCtx->streams[i]->codec->codec_type==CODEC_TYPE_AUDIO && audioStream<0) {
			audioStream=i;
		}
	}

	if(videoStream==-1 || audioStream==-1) {	// TODO: refine to work with 1 or the other
		fprintf(stdout, "INIT: Didn't find audio and video streams. Exiting.\n");
		exit(-1);
	}

	fprintf(stderr, "INIT: Num streams : %d TBR: %d %d RFRAMERATE:%d %d Duration:%ld\n", pFormatCtx->nb_streams, pFormatCtx->streams[videoStream]->time_base.num, pFormatCtx->streams[videoStream]->time_base.den, pFormatCtx->streams[videoStream]->r_frame_rate.num, pFormatCtx->streams[videoStream]->r_frame_rate.den, (long int)pFormatCtx->streams[videoStream]->duration);

	fprintf(stderr, "INIT: Video stream has id : %d\n",videoStream);
	fprintf(stderr, "INIT: Audio stream has id : %d\n",audioStream);


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

	// Figure out size
	dest_width = (dest_width > 0) ? dest_width : pCodecCtx->width;
	dest_height = (dest_height > 0) ? dest_height : pCodecCtx->height;

	//initialize outstream structures
	for (i=0; i < 1 + qualitylevels + (indexchannel?1:0); i++) {
		outstream[i].chunk = (ExternalChunk *)malloc(sizeof(ExternalChunk));
		if(!outstream[i].chunk) {
			fprintf(stderr, "INIT: Memory error alloc chunk!!!\n");
			return -1;
		}
		outstream[i].chunk->data = NULL;
		outstream[i].chunk->seq = 0;
		dcprintf(DEBUG_CHUNKER, "INIT: chunk video %d\n", outstream[i].chunk->seq);
		outstream[i].pCodecCtxEnc = NULL;
	}
	outstream[0].pCodecCtxEnc = NULL;
	for (i=1,j=1,k=1; i < 1 + qualitylevels; i++) {
		outstream[i].pCodecCtxEnc = openVideoEncoder(video_codec, video_bitrate/j, (dest_width/k/2)*2, (dest_height/k/2)*2, pCodecCtx->time_base, codec_options);	// (w/2)*2, since libx264 requires width,height to be even
		if (!outstream[i].pCodecCtxEnc) {
			return -1;
		}
		j*=3;	//reduce bitrate to 1/3
		k*=2;	//reduce dimensions to 1/2
	}
	if (indexchannel) {
		outstream[qualitylevels + 1].pCodecCtxEnc = openVideoEncoder(video_codec, 50000, 160, 120, pCodecCtx->time_base, codec_options);
		if (!outstream[qualitylevels + 1].pCodecCtxEnc) {
			return -1;
		}
	}

	fprintf(stderr, "INIT: VIDEO timebase OUT:%d %d IN: %d %d\n", outstream[1].pCodecCtxEnc->time_base.num, outstream[1].pCodecCtxEnc->time_base.den, pCodecCtx->time_base.num, pCodecCtx->time_base.den);

	if(pCodec==NULL) {
		fprintf(stderr, "INIT: Unsupported IN VIDEO pcodec!\n");
		return -1; // Codec not found
	}
	if(avcodec_open(pCodecCtx, pCodec)<0) {
		fprintf(stderr, "INIT: could not open IN VIDEO codec\n");
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
		aCodecEnc = avcodec_find_encoder_by_name(audio_codec);
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
	pFrame1=avcodec_alloc_frame();
	if(pFrame1==NULL) {
		fprintf(stderr, "INIT: Memory error alloc video frame!!!\n");
		return -1;
	}
	video_outbuf_size = STREAMER_MAX_VIDEO_BUFFER_SIZE;
	video_outbuf = av_malloc(video_outbuf_size);

	//allocate Napa-Wine transport
	frame = (Frame *)malloc(sizeof(Frame));
	if(!frame) {
		fprintf(stderr, "INIT: Memory error alloc Frame!!!\n");
		return -1;
	}

	//create empty first audio chunk

	chunkaudio = (ExternalChunk *)malloc(sizeof(ExternalChunk));
	if(!chunkaudio) {
		fprintf(stderr, "INIT: Memory error alloc chunkaudio!!!\n");
		return -1;
	}
  chunkaudio->data=NULL;
	chunkaudio->seq = 0;
	//initChunk(chunkaudio, &seq_current_chunk);
	dcprintf(DEBUG_CHUNKER, "INIT: chunk audio %d\n", chunkaudio->seq);

#ifdef HTTPIO
	/* initialize the HTTP chunk pusher */
	initChunkPusher(); //TRIPLO
#endif

	long sleep=0;
	struct timeval now_tv;
	struct timeval tmp_tv;
	long long lateTime = 0;
	long long maxAudioInterval = 0;
	long long maxVDecodeTime = 0;
//	unsigned char lastIFrameDistance = 0;

#ifdef UDPIO
	static char peer_ip[16];
	static int peer_port;
	int res = sscanf(outside_world_url, "udp://%15[0-9.]:%d", peer_ip, &peer_port);
	if (res < 2) {
		fprintf(stderr,"error parsing output url: %s\n", outside_world_url);
		return -2;
	}
	
	initUDPPush(peer_ip, peer_port);
#endif
	
	char videotrace_filename[255];
	char psnr_filename[255];
	sprintf(videotrace_filename, "yuv_data/videotrace.log");
	sprintf(psnr_filename, "yuv_data/psnrtrace.log");
	FILE* videotrace = fopen(videotrace_filename, "w");
	FILE* psnrtrace = fopen(psnr_filename, "w");

#ifdef USE_AVFILTER
	//init AVFilter
	avfilter_register_all();
	init_filters(avfilter, pCodecCtx);
#endif

	//main loop to read from the input file
	while((av_read_frame(pFormatCtx, &packet)>=0) && !quit)
	{
		//detect if a strange number of anomalies is occurring
		if(ptsvideo1 < 0 || ptsvideo1 > packet.dts || ptsaudio1 < 0 || ptsaudio1 > packet.dts) {
			pts_anomalies_counter++;
			dctprintf(DEBUG_ANOMALIES, "READLOOP: pts BASE anomaly detected number %d (a:%"PRId64" v:%"PRId64" dts:%"PRId64")\n", pts_anomalies_counter, ptsaudio1, ptsvideo1, packet.dts);
			if(pts_anomaly_threshold >=0 && live_source) { //reset just in case of live source
				if(pts_anomalies_counter > pts_anomaly_threshold) {
					dctprintf(DEBUG_ANOMALIES, "READLOOP: too many pts BASE anomalies. resetting pts base\n");
					av_free_packet(&packet);
					goto close;
				}
			}
		}

		//newTime_video and _audio are in usec
		//if video and audio stamps differ more than 5sec
		if( newTime_video - newTime_audio > 5000000 || newTime_video - newTime_audio < -5000000 ) {
			newtime_anomalies_counter++;
			dctprintf(DEBUG_ANOMALIES, "READLOOP: NEWTIME audio video differ anomaly detected number %d (a:%lld, v:%lld)\n", newtime_anomalies_counter, newTime_audio, newTime_video);
		}

		if(newtime_anomaly_threshold >=0 && newtime_anomalies_counter > newtime_anomaly_threshold) {
			if(live_source) { //restart just in case of live source
				dctprintf(DEBUG_ANOMALIES, "READLOOP: too many NEGATIVE TIMESTAMPS anomalies. Restarting.\n");
				av_free_packet(&packet);
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
					if(timebank && (lateTime+maxVDecodeTime) >= 0)
					{
						dcprintf(DEBUG_ANOMALIES, "\n\n\t\t************************* SKIPPING VIDEO FRAME %ld ***********************************\n\n", sleep);
						av_free_packet(&packet);
						continue;
					}
				}
			}
			
			gettimeofday(&tmp_tv, NULL);
			
			//decode the video packet into a raw pFrame

			if(avcodec_decode_video2(pCodecCtx, pFrame1, &frameFinished, &packet)>0)
			{
				AVFrame *pFrame;
				pFrame = pFrame1;

				// usleep(5000);
				dctprintf(DEBUG_VIDEO_FRAMES, "VIDEOin pkt: dts %"PRId64" pts %"PRId64" pts-dts %"PRId64"\n", packet.dts, packet.pts, packet.pts-packet.dts );
				dcprintf(DEBUG_VIDEO_FRAMES, "VIDEOdecode: pkt_dts %"PRId64" pkt_pts %"PRId64" frame.pts %"PRId64"\n", pFrame->pkt_dts, pFrame->pkt_pts, pFrame->pts);
				dcprintf(DEBUG_VIDEO_FRAMES, "VIDEOdecode intype %d%s\n", pFrame->pict_type, pFrame->key_frame ? " (key)" : "");

				if(frameFinished)
				{ // it must be true all the time else error
					AVFrame *pFrame2 = NULL;

					frame->number = ++contFrameVideo;



					dcprintf(DEBUG_VIDEO_FRAMES, "VIDEO: finished frame %d dts %"PRId64" pts %"PRId64"\n", frame->number, packet.dts, packet.pts);
					if(frame->number==0) {
						if(packet.dts==AV_NOPTS_VALUE)
						{
							//a Dts with a noPts value is troublesome case for delta calculation based on Dts
							contFrameVideo = STREAMER_MAX(contFrameVideo-1, 0);
							av_free_packet(&packet);
							continue;
						}
						last_pkt_dts = packet.dts;
						newTime = 0;
					}
					else {
						if(packet.dts!=AV_NOPTS_VALUE) {
							delta_video = packet.dts-last_pkt_dts;
							last_pkt_dts = packet.dts;
						}
						else if(delta_video==0)
						{
							//a Dts with a noPts value is troublesome case for delta calculation based on Dts
							contFrameVideo = STREAMER_MAX(contFrameVideo-1, 0);
							av_free_packet(&packet);
							continue;
						}
					}
					dcprintf(DEBUG_VIDEO_FRAMES, "VIDEO: deltavideo : %d\n", (int)delta_video);

					//set initial timestamp
					if(FirstTimeVideo && pFrame->pkt_pts>0) {
						if(offset_av) {
							ptsvideo1 = pFrame->pkt_pts;
							FirstTimeVideo = 0;
							dcprintf(DEBUG_VIDEO_FRAMES, "VIDEO: SET PTS BASE OFFSET %"PRId64"\n", ptsvideo1);
						} else { //we want to compensate audio and video offset for this source
							//maintain the offset between audio pts and video pts
							//because in case of live source they have the same numbering
							if(ptsaudio1 > 0) //if we have already seen some audio frames...
								ptsvideo1 = ptsaudio1;
							else
								ptsvideo1 = pFrame->pkt_pts;
							FirstTimeVideo = 0;
							dcprintf(DEBUG_VIDEO_FRAMES, "VIDEO LIVE: SET PTS BASE OFFSET %"PRId64"\n", ptsvideo1);
						}
					}

					// store timestamp in useconds for next frame sleep
					if (pFrame->pkt_pts != AV_NOPTS_VALUE) {
						newTime_video = pts2ms(pFrame->pkt_pts - ptsvideo1, pFormatCtx->streams[videoStream]->time_base)*1000;
					} else {
						newTime_video = pts2ms(pFrame->pkt_dts - ptsvideo1, pFormatCtx->streams[videoStream]->time_base)*1000;	//TODO: a better estimate is needed
					}
					dcprintf(DEBUG_VIDEO_FRAMES, "Setting v:%lld\n", newTime_video);

					if(true) {	//copy channel
						video_frame_size = packet.size;
						if (video_frame_size > video_outbuf_size) {
							fprintf(stderr, "VIDEO: error, outbuf too small, SKIPPING\n");;
							av_free_packet(&packet);
							continue;
						} else {
							memcpy(video_outbuf, packet.data, video_frame_size);
						}

						if (pFrame->pkt_pts != AV_NOPTS_VALUE) {
							target_pts = pFrame->pkt_pts;
						}else {	//TODO: review this
							target_pts = pFrame->pkt_dts;
						}
						createFrame(frame, pts2ms(target_pts - ptsvideo1, pFormatCtx->streams[videoStream]->time_base), video_frame_size, 
					            pFrame->pict_type);
						addFrameToOutstream(&outstream[0], frame, video_outbuf);
					}

					if (pFrame->pkt_pts != AV_NOPTS_VALUE) {
						pFrame->pts = av_rescale_q(pFrame->pkt_pts, pFormatCtx->streams[videoStream]->time_base, outstream[1].pCodecCtxEnc->time_base);
					} else {	//try to figure out the pts //TODO: review this
						if (pFrame->pkt_dts != AV_NOPTS_VALUE) {
							pFrame->pts = av_rescale_q(pFrame->pkt_dts, pFormatCtx->streams[videoStream]->time_base, outstream[1].pCodecCtxEnc->time_base);
						}
					}

					pFrame2 = preprocessFrame(pFrame);
					if (pFrame2) pFrame = pFrame2;

					for (i=1; i < 1 + qualitylevels + (indexchannel?1:0); i++) {
						video_frame_size = transcodeFrame(video_outbuf, video_outbuf_size, &target_pts, pFrame, pFormatCtx->streams[videoStream]->time_base, pCodecCtx, outstream[i].pCodecCtxEnc);
						if (video_frame_size <= 0) {
							av_free_packet(&packet);
							contFrameVideo = STREAMER_MAX(contFrameVideo-1, 0);
							continue;	//TODO: this seems wrong, continuing the internal cycle
						}
						createFrame(frame, pts2ms(target_pts - ptsvideo1, pFormatCtx->streams[videoStream]->time_base), video_frame_size,
					            (unsigned char)outstream[i].pCodecCtxEnc->coded_frame->pict_type);
						addFrameToOutstream(&outstream[i], frame, video_outbuf);
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
								dcprintf(DEBUG_ANOMALIES,"\tmaxDelay=%ld\n", ((long)maxDelay));
								dcprintf(DEBUG_ANOMALIES,"\tlast video frame interval=%ld; sleep time=%ld\n", ((long)usec), ((long)sleep));
							}
							else
								sleep = 0;

							//update and store counters
							newTime_prev = newTime_video;

							//i can also sleep now instead of at the beginning of
							//the next frame because in this case we only have video
							//frames, hence it would immediately be the next thing to do
							if(sleep > 0) {
								dcprintf(DEBUG_TIMESTAMPING,"\n\tREADLOOP: going to sleep for %ld microseconds\n", sleep);
								usleep(sleep);
							}

						}
					}
					if(pFrame2) av_free(pFrame2);
				}
			}
		} else if(packet.stream_index==audioStream) {
			if(sleep > 0)
			{
				dcprintf(DEBUG_TIMESTAMPING, "\n\tREADLOOP: going to sleep for %ld microseconds\n", sleep);
				usleep(sleep);
			}
			
			audio_data_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
			//decode the audio packet into a raw audio source buffer
			if(avcodec_decode_audio3(aCodecCtx, samples, &audio_data_size, &packet)>0)
			{
				dcprintf(DEBUG_AUDIO_FRAMES, "\n-------AUDIO FRAME\n");
				dcprintf(DEBUG_AUDIO_FRAMES, "AUDIO: newTimeaudioSTART : %lf\n", (double)(packet.pts)*av_q2d(pFormatCtx->streams[audioStream]->time_base));
				if(audio_data_size>0) {
					dcprintf(DEBUG_AUDIO_FRAMES, "AUDIO: datasizeaudio:%d\n", audio_data_size);
					/* if a frame has been decoded, output it */
					//fwrite(samples, 1, audio_data_size, outfileaudio);
				}
				else {
					av_free_packet(&packet);
					continue;
				}
	
				audio_frame_size = avcodec_encode_audio(aCodecCtxEnc, audio_outbuf, audio_data_size, samples);
				if(audio_frame_size <= 0) {
					av_free_packet(&packet);
					continue;
				}
				
				frame->number = contFrameAudio;

				if(frame->number==0) {
					if(packet.dts==AV_NOPTS_VALUE) {
						av_free_packet(&packet);
						continue;
					}
					last_pkt_dts_audio = packet.dts;
					newTime = 0;
				}
				else {
					if(packet.dts!=AV_NOPTS_VALUE) {
						delta_audio = packet.dts-last_pkt_dts_audio;
						last_pkt_dts_audio = packet.dts;
					}
					else if(delta_audio==0) {
						av_free_packet(&packet);
						continue;
					}
				}
				dcprintf(DEBUG_AUDIO_FRAMES, "AUDIO: original codec frame number %d vs. encoded %d vs. packed %d\n", aCodecCtx->frame_number, aCodecCtxEnc->frame_number, frame->number);
				//use pts if dts is invalid
				if(packet.dts!=AV_NOPTS_VALUE)
					target_pts = packet.dts;
				else if(packet.pts!=AV_NOPTS_VALUE) {
					target_pts = packet.pts;
				} else  {
					av_free_packet(&packet);
					continue;
				}

				if(offset_av)
				{
					if(FirstTimeAudio && packet.dts>0) {
						ptsaudio1 = packet.dts;
						FirstTimeAudio = 0;
						dcprintf(DEBUG_AUDIO_FRAMES, "AUDIO: SET PTS BASE OFFSET %"PRId64"\n", ptsaudio1);
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
							ptsaudio1 = packet.dts;
						FirstTimeAudio = 0;
						dcprintf(DEBUG_AUDIO_FRAMES, "AUDIO LIVE: SET PTS BASE OFFSET %"PRId64"\n", ptsaudio1);
					}
				}
				//compute the new audio timestamps in milliseconds
				if(frame->number>0) {
					newTime = ((target_pts-ptsaudio1)*1000.0*((double)av_q2d(pFormatCtx->streams[audioStream]->time_base)));//*(double)delta_audio;
					// store timestamp in useconds for next frame sleep
					newTime_audio = newTime*1000;
				}
				dcprintf(DEBUG_TIMESTAMPING, "AUDIO: NEWTIMESTAMP %lld\n", newTime);
				if(newTime<0) {
					dcprintf(DEBUG_AUDIO_FRAMES, "AUDIO: SKIPPING FRAME\n");
					newtime_anomalies_counter++;
					dctprintf(DEBUG_ANOMALIES, "READLOOP: NEWTIME negative audio timestamp anomaly detected number %d (a:%lld)\n", newtime_anomalies_counter, newTime*1000);
					av_free_packet(&packet);
					continue; //SKIP THIS FRAME, bad timestamp
				}

				frame->timestamp.tv_sec = (unsigned int)(newTime + delay_audio)/1000;
				frame->timestamp.tv_usec = (newTime + delay_audio)%1000;
				frame->size = audio_frame_size;
				frame->type = 5; // 5 is audio type
				dcprintf(DEBUG_AUDIO_FRAMES, "AUDIO: pts %"PRId64" duration %d timebase %d %d dts %"PRId64"\n", packet.pts, packet.duration, pFormatCtx->streams[audioStream]->time_base.num, pFormatCtx->streams[audioStream]->time_base.den, packet.dts);
				dcprintf(DEBUG_AUDIO_FRAMES, "AUDIO: timestamp sec:%ld usec:%ld\n", (long)frame->timestamp.tv_sec, (long)frame->timestamp.tv_usec);
				dcprintf(DEBUG_AUDIO_FRAMES, "AUDIO: deltaaudio %"PRId64"\n", delta_audio);	
				contFrameAudio++;

				if(update_chunk(chunkaudio, frame, audio_outbuf) == -1) {
					fprintf(stderr, "AUDIO: unable to update chunk %d. Exiting.\n", chunkaudio->seq);
					exit(-1);
				}
				//set priority
				chunkaudio->priority = 1;

				if(chunkFilled(chunkaudio, AUDIO_CHUNK)) {
					// is chunk filled using current strategy?
					//SAVE ON FILE
					//saveChunkOnFile(chunkaudio);
					//Send the chunk to an external transport/player
					for (i=0; i < 1 + qualitylevels; i++) {	//do not send audio to the index channel
						sendChunk(outstream[i].output, chunkaudio);
					}
					dctprintf(DEBUG_CHUNKER, "AUDIO: just sent chunk audio %d\n", chunkaudio->seq);
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
						dcprintf(DEBUG_TIMESTAMPING,"\tmaxDelay=%ld, maxAudioInterval=%ld\n", ((long)maxDelay), ((long) maxAudioInterval));
						dcprintf(DEBUG_TIMESTAMPING,"\tlast audio frame interval=%ld; lateTime=%ld\n", ((long)usec), ((long)lateTime));

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
		dcprintf(DEBUG_CHUNKER,"Free the packet that was allocated by av_read_frame\n");
		av_free_packet(&packet);
	}
	
	if(videotrace)
		fclose(videotrace);
	if(psnrtrace)
		fclose(psnrtrace);

close:
	for (i=0; i < 1 + qualitylevels + (indexchannel?1:0); i++) {
		if(outstream[i].chunk->seq != 0 && outstream[i].chunk->frames_num>0) {
			sendChunk(outstream[i].output, outstream[0].chunk);
			dcprintf(DEBUG_CHUNKER, "CHUNKER: SENDING LAST VIDEO CHUNK\n");
			outstream[i].chunk->seq = 0; //signal that we need an increase just in case we will restart
		}
	}
	for (i=0; i < 1 + qualitylevels; i++) {
		if(chunkaudio->seq != 0 && chunkaudio->frames_num>0) {
			sendChunk(outstream[i].output, chunkaudio);
			dcprintf(DEBUG_CHUNKER, "CHUNKER: SENDING LAST AUDIO CHUNK\n");
		}
	}
	chunkaudio->seq = 0; //signal that we need an increase just in case we will restart

#ifdef HTTPIO
	/* finalize the HTTP chunk pusher */
	finalizeChunkPusher();
#endif

	for (i=0; i < 1 + qualitylevels + (indexchannel?1:0); i++) {
		free(outstream[i].chunk);
	}
	free(chunkaudio);
	free(frame);
	av_free(video_outbuf);
	av_free(audio_outbuf);
	free(cmeta);

	// Free the YUV frame
	av_free(pFrame1);
	av_free(samples);
  
	// Close the codec
	avcodec_close(pCodecCtx);
	for (i=1; i < 1 + qualitylevels + (indexchannel?1:0); i++) {
		avcodec_close(outstream[i].pCodecCtxEnc);
	}

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
		dcprintf(DEBUG_CHUNKER, "CHUNKER: WAITING 5 secs FOR LIVE SOURCE TO SKIP ERRORS AND RESTARTING\n");
		videoStream = -1;
		audioStream = -1;
		FirstTimeAudio=1;
		FirstTimeVideo=1;
		pts_anomalies_counter=0;
		newtime_anomalies_counter=0;
		newTime=0;
		newTime_audio=0;
		newTime_prev=0;
		ptsvideo1=0;
		ptsaudio1=0;
		last_pkt_dts=0;
		delta_video=0;
		delta_audio=0;
		last_pkt_dts_audio=0;
		target_pts=0;
		i=0;
		//~ contFrameVideo = 0;
		//~ contFrameAudio = 1;
		
#ifdef YUV_RECORD_ENABLED
		if(ChunkerStreamerTestMode)
		{
			video_record_count++;
			//~ savedVideoFrames = 0;
			
			//~ char tmp_filename[255];
			//~ sprintf(tmp_filename, "yuv_data/out_%d.yuv", video_record_count);
			//~ FILE *pFile=fopen(tmp_filename, "w");
			//~ if(pFile!=NULL)
				//~ fclose(pFile);
		}
#endif

		goto restart;
	}

#ifdef TCPIO
	for (i=0; i < 1 + qualitylevels + (indexchannel?1:0); i++) {
		finalizeTCPChunkPusher(outstream[i].output);
	}
#endif

#ifdef USE_AVFILTER
	close_filters();
#endif

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
	//add frame priority to chunk priority (to be normalized later on)
	chunk->priority += frame->type + 1; // I:2, P:3, B:4

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

void SaveFrame(AVFrame *pFrame, int width, int height)
{
	FILE *pFile;
	int  y;

	 // Open file
	char tmp_filename[255];
	sprintf(tmp_filename, "yuv_data/streamer_out.yuv");
	pFile=fopen(tmp_filename, "ab");
	if(pFile==NULL)
		return;

	// Write header
	//fprintf(pFile, "P5\n%d %d\n255\n", width, height);
  
	// Write Y data
	for(y=0; y<height; y++)
  		if(fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width, pFile) != width)
		{
			printf("errno = %d\n", errno);
			exit(1);
		}
	// Write U data
	for(y=0; y<height/2; y++)
  		if(fwrite(pFrame->data[1]+y*pFrame->linesize[1], 1, width/2, pFile) != width/2)
  		{
			printf("errno = %d\n", errno);
			exit(1);
		}
	// Write V data
	for(y=0; y<height/2; y++)
  		if(fwrite(pFrame->data[2]+y*pFrame->linesize[2], 1, width/2, pFile) != width/2)
  		{
			printf("errno = %d\n", errno);
			exit(1);
		}
  
	// Close file
	fclose(pFile);
}

void SaveEncodedFrame(Frame* frame, uint8_t *video_outbuf)
{
	static FILE* pFile = NULL;
	
	pFile=fopen("yuv_data/streamer_out.mpeg4", "ab");
	fwrite(frame, sizeof(Frame), 1, pFile);
	fwrite(video_outbuf, frame->size, 1, pFile);
	fclose(pFile);
}
