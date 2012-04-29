#ifndef _CHUNKER_STREAMER_H
#define _CHUNKER_STREAMER_H

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "streamer_commons.h"
#include "chunker_metadata.h"
#include "codec_definitions.h"
#include "frame.h"

//int ChunkerStreamerTestMode;

#define STREAMER_MAX_VIDEO_BUFFER_SIZE 200000
#define STREAMER_MAX_AUDIO_BUFFER_SIZE 10000

#ifdef __LINUX__
#define DELETE_DIR(folder) {char command_name[255]; sprintf(command_name, "rm -fR %s", folder); system(command_name); }
#define CREATE_DIR(folder) {char command_name[255]; sprintf(command_name, "mkdir %s", folder); system(command_name); }
#else
#define DELETE_DIR(folder) {char command_name[255]; sprintf(command_name, "rd /S /Q %s", folder); system(command_name); }
#define CREATE_DIR(folder) {char command_name[255]; sprintf(command_name, "mkdir %s", folder); system(command_name); }
#endif

#define LOOP_MODE 1
#define YUV_RECORD_ENABLED
//~ #define STREAMER_X264_USE_SSIM
//~ #define VIDEO_DEINTERLACE

#endif
