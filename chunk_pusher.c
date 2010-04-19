#include <stdlib.h>
#include <string.h>

#include "external_chunk_transcoding.h"
#include "chunker_streamer.h"

#define DEBUG_PUSHER


int pushChunkHttp(ExternalChunk *echunk, char *url);

extern ChunkerMetadata *cmeta;


int pushChunkHttp(ExternalChunk *echunk, char *url) {
	Chunk gchunk;
	void *grapes_chunk_attributes_block=NULL;
	static size_t attr_size = 0;
	int ts1e3 = 0.0;
	int tus3e1 = 0.0;
	int ret = STREAMER_FAIL_RETURN;
	
	attr_size = 5*sizeof(int) + 2*sizeof(time_t) + 2*sizeof(suseconds_t) + 1*sizeof(double);

	/* first pack the chunk info that we get from the streamer into an "attributes" block of a regular GRAPES chunk */
	if(	(grapes_chunk_attributes_block = packExternalChunkToAttributes(echunk, attr_size)) != NULL ) {
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
		gchunk.attributes_size = attr_size;
		gchunk.data = echunk->data;

#ifdef NHIO
			write_chunk(&gchunk);
#else
			ret = sendViaCurl(gchunk, attr_size, echunk, url);
#endif

		free(grapes_chunk_attributes_block);
		return ret;
	}
	return ret;
}
