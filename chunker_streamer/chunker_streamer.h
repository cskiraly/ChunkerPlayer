#ifndef _CHUNKER_STREAMER_H
#define _CHUNKER_STREAMER_H


#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>

#include "chunker_metadata.h"
#include "frame.h"
#include "codec_definitions.h"
#include "external_chunk_transcoding.h"


#define STREAMER_FAIL_RETURN -1
#define STREAMER_OK_RETURN 0

#define STREAMER_MAX_VIDEO_BUFFER_SIZE 200000
#define STREAMER_MAX_AUDIO_BUFFER_SIZE 10000


#endif
