/* 
 * File:   audio.h
 * Author: carmelo
 *
 * Created on December 17, 2009, 12:32 PM
 */

#ifndef _AUDIO_H
#define	_AUDIO_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <alsa/asoundlib.h>
#include <stdlib.h>

#define AUDIO_PERIOD 32

static int err;
static short buf[128];
static snd_pcm_t* capture_handle;
static snd_pcm_hw_params_t* hw_params;
static char* audio_buffer;
static int buffer_size;
static int frames;
int audio_s_bit;

void process_sample(unsigned char* buffer, int buffer_size);
int init_audio(char* audio_dev_name);
void close_audio_device();

#ifdef	__cplusplus
}
#endif

#endif	/* _AUDIO_H */

