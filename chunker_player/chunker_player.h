#ifndef _CHUNKER_PLAYER_H
#define _CHUNKER_PLAYER_H


#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <microhttpd.h>

#include "external_chunk_transcoding.h"
#include "frame.h"

#define PLAYER_FAIL_RETURN -1
#define PLAYER_OK_RETURN 0

#define FULLSCREEN_WIDTH 640
#define FULLSCREEN_HEIGHT 480

AVCodecContext  *aCodecCtx;

int window_width, window_height;


#endif
