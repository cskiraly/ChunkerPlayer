/*
 *  Copyright (c) 2009-2011 Carmelo Daniele, Dario Marchese, Diego Reforgiato, Giuseppe Tropea
 *  developed for the Napa-Wine EU project. See www.napa-wine.eu
 *
 *  This is free software; see lgpl-2.1.txt
 */

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "external_chunk_transcoding.h"
#include "chunker_streamer.h"

//#define DEBUG_PUSHER


int pushChunkHttp(ExternalChunk *echunk, char *url);
int pushChunkTcp(ExternalChunk *echunk);

extern ChunkerMetadata *cmeta;
static long long int counter = 0;
static int fd = -1;

void initUDPPush(char* peer_ip, int peer_port)
{
	if(fd == -1)
	{
		fd = socket(AF_INET, SOCK_DGRAM, 0);
	
		struct sockaddr_in address;
		address.sin_family = AF_INET; 
		address.sin_addr.s_addr = inet_addr(peer_ip);
		address.sin_port = htons(peer_port);
		 
		int result = connect(fd, (struct sockaddr *)&address, sizeof(struct sockaddr_in));
		if(result == -1){
			fprintf(stderr, "UDP OUTPUT MODULE: could not connect to the peer!\n");
			exit(1);
		}
	}
}

void finalizeUDPChunkPusher()
{
	if(fd > 0)
	{
		close(fd);
		fd = -1;
	}
}

int pushChunkUDP(ExternalChunk *echunk) {

	Chunk gchunk;
	void *grapes_chunk_attributes_block = NULL;
	int ret = STREAMER_FAIL_RETURN;
	//we need to pack 5 int32s + 2 timeval structs + 1 double
	static size_t ExternalChunk_header_size = 5*CHUNK_TRANSCODING_INT_SIZE + 2*CHUNK_TRANSCODING_INT_SIZE + 2*CHUNK_TRANSCODING_INT_SIZE + 1*CHUNK_TRANSCODING_INT_SIZE*2;
	
	//update the chunk len here because here we know the external chunk header size
	echunk->len = echunk->payload_len + ExternalChunk_header_size;

	/* first pack the chunk info that we get from the streamer into an "attributes" block of a regular GRAPES chunk */
	if(	(grapes_chunk_attributes_block = packExternalChunkToAttributes(echunk, ExternalChunk_header_size)) != NULL ) {
		struct timeval now;

		/* then fill-up a proper GRAPES chunk */
		gchunk.size = echunk->payload_len;
		/* then fill the timestamp */
		gettimeofday(&now, NULL);
		gchunk.timestamp = now.tv_sec * 1000000ULL + now.tv_usec;

		//decide how to create the chunk ID
		if(cmeta->cid == 0) {
			gchunk.id = echunk->seq;
#ifdef DEBUG_PUSHER
			fprintf(stderr, "PUSHER: packaged SEQ chunkID %d\n", gchunk.id);
#endif
		}
		else if(cmeta->cid == 1) {
			gchunk.id = gchunk.timestamp; //its ID is its start time
#ifdef DEBUG_PUSHER
			fprintf(stderr, "PUSHER: packaged TS chunkID %d\n", gchunk.id);
#endif
		}
		else if(cmeta->cid == 2) {
			//its ID is offset by actual time in seconds
			gchunk.id = ++counter + cmeta->base_chunkid_sequence_offset;
#ifdef DEBUG_PUSHER
			fprintf(stderr, "PUSHER: packaged SEQ %d + %d offset chunkID %d\n", echunk->seq, cmeta->base_chunkid_sequence_offset, gchunk.id);
#endif
		}
		gchunk.attributes = grapes_chunk_attributes_block;
		gchunk.attributes_size = ExternalChunk_header_size;
		gchunk.data = echunk->data;

		/* 20 bytes are needed to put the chunk header info on the wire + attributes size + payload */
		ret = sendViaUDP(gchunk, GRAPES_ENCODED_CHUNK_HEADER_SIZE + gchunk.attributes_size + gchunk.size);

		free(grapes_chunk_attributes_block);
		return ret;
	}
	return ret;
}

int sendViaUDP(Chunk gchunk, int buffer_size)
{
	uint8_t *buffer=NULL;

	int ret = STREAMER_FAIL_RETURN;
	
	if(!(fd > 0))
	{
		fprintf(stderr, "IO-MODULE: trying to send data to a not connected socket!!!\n");
		return ret;
	}

	if( (buffer = malloc(buffer_size)) != NULL) {
		/* encode the GRAPES chunk into network bytes */
		encodeChunk(&gchunk, buffer, buffer_size);
		
		int ret = send(fd, buffer, buffer_size, 0);
		int tmp;
		while(ret != buffer_size)
		{
			tmp = send(fd, buffer+ret, buffer_size-ret, 0);
			if(tmp > 0)
				ret += tmp;
			else
				break;
		}
		
		free(buffer);
	}
	return ret;
}
