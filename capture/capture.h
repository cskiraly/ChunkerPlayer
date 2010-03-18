/* 
 * File:   capture.h
 * Author: carmelo
 *
 * Created on December 17, 2009, 6:20 PM
 */

#ifndef _CAPTURE_H
#define	_CAPTURE_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>

FILE * img;
unsigned int seq;
char filename[255];
static unsigned char* rgb_buffer = NULL;
pthread_t video_capure_thread;
pthread_t audio_capure_thread;
FILE* audio_output_file;
unsigned char running;

#define DEFAULT_VIDEO_FORMAT V4L2_PIX_FMT_YUYV
#define DEFAULT_VIDEO_DEVICE "/dev/video0"
#define DEFAULT_AUDIO_OUT_FILE "out.wav"
#define WIDTH 640
#define HEIGHT 480

#define AUDIO_SAMPLE_RATE 44100 // ( 44100 = CD quality)
#define AUDIO_CHANNELS 2
#define AUDIO_FORMAT SND_PCM_FORMAT_S16_LE
#define AUDIO_INTERLEAVED SND_PCM_ACCESS_RW_INTERLEAVED

/*
* The WAVE Header
*/
typedef struct wav_s {
	char chunk_id[4] ; /* "RIFF" */
	unsigned int chunk_size; /* n*4 + 36 */
	char format[4]; /* "WAVE" */
	char sub_chunk_id[4]; /* "fmt " */
	unsigned int sub_chunk_size; /* 16 */
	unsigned short audio_format; /* 1 */
	unsigned short num_channels; /* 2 */
	unsigned int sample_rate; /* 44100 */
	unsigned int byte_rate; /* 4*44100 */
	unsigned short block_alg; /* 4 */
	unsigned short bps; /* 16 */
	unsigned char sub_chunk2_id[4]; /* "data" */
	unsigned int sub_chunk2_size; /* n*4 */
} wav_t ;

void* video_capture(void* ThreadParams);
void* audio_capture(void* ThreadParams);
void terminate(int signum);

#ifdef	__cplusplus
}
#endif

#endif	/* _CAPTURE_H */

