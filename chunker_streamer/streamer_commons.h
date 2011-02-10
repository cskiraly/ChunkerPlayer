#ifndef _STREAMER_COMMONS_H
#define _STREAMER_COMMONS_H


#include <stdio.h>
#include <unistd.h>
#include <memory.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#else
#include <winsock2.h>
#endif

#include "external_chunk_transcoding.h"


#define STREAMER_FAIL_RETURN -1
#define STREAMER_OK_RETURN 0


#endif
