#include <stdlib.h>
#include <string.h>

#include "external_chunk_transcoding.h"
#include "chunker_streamer.h"

//#define DEBUG_PUSHER


int pushChunkHttp(ExternalChunk *echunk, char *url);

extern ChunkerMetadata *cmeta;


int pushChunkHttp(ExternalChunk *echunk, char *url) {
	Chunk gchunk;
	void *grapes_chunk_attributes_block = NULL;
	int ts1e3 = 0.0;
	int tus3e1 = 0.0;
	int ret = STREAMER_FAIL_RETURN;
	//we need to pack 5 int32s + 2 timeval structs + 1 double
	static size_t ExternalChunk_header_size = 5*CHUNK_TRANSCODING_INT_SIZE + 2*CHUNK_TRANSCODING_INT_SIZE + 2*CHUNK_TRANSCODING_INT_SIZE + 1*CHUNK_TRANSCODING_INT_SIZE*2;
	
	//update the chunk len here because here we know the external chunk header size
	echunk->len = echunk->payload_len + ExternalChunk_header_size;

	/* first pack the chunk info that we get from the streamer into an "attributes" block of a regular GRAPES chunk */
	if(	(grapes_chunk_attributes_block = packExternalChunkToAttributes(echunk, ExternalChunk_header_size)) != NULL ) {
		/* then fill-up a proper GRAPES chunk */
		gchunk.size = echunk->payload_len;
		/* convert external_chunk start_time in milliseconds and put it in grapes chunk */
		ts1e3 = (int)(echunk->start_time.tv_sec*1e3);
		//tus3e1 = (int)(echunk->start_time.tv_usec/1e3); ALREADY IN MILLI SECONDS
		gchunk.timestamp = ts1e3+(int)(echunk->start_time.tv_usec);
#ifdef DEBUG_PUSHER
			fprintf(stderr, "PUSHER: start sec %d usec %d timestamp %ld\n", ts1e3, (int)(echunk->start_time.tv_usec), gchunk.timestamp);
#endif

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
		gchunk.attributes = grapes_chunk_attributes_block;
		gchunk.attributes_size = ExternalChunk_header_size;
		gchunk.data = echunk->data;

#ifdef NHIO
		write_chunk(&gchunk);
#else
		/* 20 bytes are needed to put the chunk header info on the wire + attributes size + payload */
		ret = sendViaCurl(gchunk, GRAPES_ENCODED_CHUNK_HEADER_SIZE + gchunk.attributes_size + gchunk.size, url);
#endif

		free(grapes_chunk_attributes_block);
		return ret;
	}
	return ret;
}
