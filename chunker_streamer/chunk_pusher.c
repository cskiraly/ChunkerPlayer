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
int sendViaCurl(Chunk gchunk, int buffer_size, char *url);
int sendViaTcp(Chunk gchunk, uint32_t buffer_size);


extern ChunkerMetadata *cmeta;
static long long int counter = 0;
static int tcp_fd = -1;
static bool tcp_fd_connected = false;
static char* peer_ip;
static int peer_port;
static bool exit_on_connect_failure = false;
static bool connect_on_data = true;
static bool exit_on_send_error = false;

void initTCPPush(char* ip, int port)
{
	peer_ip = strdup(ip);
	peer_port = port;

	if(tcp_fd == -1)
	{
		tcp_fd=socket(AF_INET, SOCK_STREAM, 0);
	}
	if (!tcp_fd_connected) {
		struct sockaddr_in address;
		address.sin_family = AF_INET; 
		address.sin_addr.s_addr = inet_addr(peer_ip);
		address.sin_port = htons(peer_port);
	 
		int result = connect(tcp_fd, (struct sockaddr *)&address, sizeof(struct sockaddr_in));
		if(result == -1){
			fprintf(stderr, "TCP OUTPUT MODULE: could not connect to the peer!\n");
			if (exit_on_connect_failure) {
				exit(1);
			}
			tcp_fd_connected = false;
		} else {
			tcp_fd_connected = true;
		}
	}
}

void finalizeTCPChunkPusher()
{
	if(tcp_fd > 0)
	{
		close(tcp_fd);
		tcp_fd = -1;
	}
}

int pushChunkHttp(ExternalChunk *echunk, char *url) {

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

#ifdef NHIO
		write_chunk(&gchunk);
#else
		/* 20 bytes are needed to put the chunk header info on the wire + attributes size + payload */
		ret = sendViaCurl(gchunk, GRAPES_ENCODED_CHUNK_HEADER_SIZE + gchunk.attributes_size + gchunk.size, url);
		//~ if(ChunkerStreamerTestMode)
			//~ ret = sendViaCurl(gchunk, GRAPES_ENCODED_CHUNK_HEADER_SIZE + gchunk.attributes_size + gchunk.size, "http://localhost:5557/externalplayer");
#endif

		free(grapes_chunk_attributes_block);
		return ret;
	}
	return ret;
}

int pushChunkTcp(ExternalChunk *echunk)
{
	Chunk gchunk;
	void *grapes_chunk_attributes_block = NULL;
	int ret = STREAMER_FAIL_RETURN;
	//we need to pack 5 int32s + 2 timeval structs + 1 double
	static size_t ExternalChunk_header_size = 5*CHUNK_TRANSCODING_INT_SIZE + 2*CHUNK_TRANSCODING_INT_SIZE + 2*CHUNK_TRANSCODING_INT_SIZE + 1*CHUNK_TRANSCODING_INT_SIZE*2;
	
	//try to connect if not connected
	if (connect_on_data && !tcp_fd_connected) {
		initTCPPush(peer_ip, peer_port);
		if (!tcp_fd_connected) {
			return ret;
		}
	}

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

#ifdef NHIO
		write_chunk(&gchunk);
#else
		/* 20 bytes are needed to put the chunk header info on the wire + attributes size + payload */
		ret = sendViaTcp(gchunk, GRAPES_ENCODED_CHUNK_HEADER_SIZE + gchunk.attributes_size + gchunk.size);
#endif

		free(grapes_chunk_attributes_block);
		return ret;
	}
	return ret;
}

int sendViaTcp(Chunk gchunk, uint32_t buffer_size)
{
	uint8_t *buffer=NULL;

	int ret = STREAMER_FAIL_RETURN;
	
	if(!(tcp_fd > 0))
	{
		fprintf(stderr, "TCP IO-MODULE: trying to send data to a not connected socket!!!\n");
		return ret;
	}

	if( (buffer = malloc(4 + buffer_size)) != NULL) {
		/* encode the GRAPES chunk into network bytes */
		encodeChunk(&gchunk, buffer + 4, buffer_size);
		*(uint32_t*)buffer = htonl(buffer_size);
		
		int ret = send(tcp_fd, buffer, 4 + buffer_size, exit_on_send_error ? 0 : MSG_NOSIGNAL); //TODO: better handling of exit_on_send_error
fprintf(stderr, "TCP IO-MODULE: sending %d bytes, %d sent\n", buffer_size, ret);
		if (ret < 0) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				fprintf(stderr, "TCP IO-MODULE: closing connection\n");
				close(tcp_fd);
				tcp_fd = -1;
				tcp_fd_connected = false;
			}
			return ret;
		}
		int tmp;
		while(ret != buffer_size)
		{
			tmp = send(tcp_fd, buffer+ret, 4 + buffer_size - ret, exit_on_send_error ? 0 : MSG_NOSIGNAL);
			if(tmp > 0)
				ret += tmp;
			else
				break;
		}
		
		free(buffer);
	}
	return ret;
}
