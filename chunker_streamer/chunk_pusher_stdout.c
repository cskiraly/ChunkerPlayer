#include <stdlib.h>
#include <string.h>

#include "external_chunk_transcoding.h"
#include "chunker_streamer.h"

void initChunkPusher();
void finalizeChunkPusher();
int pushChunkHttp(ExternalChunk *echunk, char *url);
void *chunkToAttributes(ExternalChunk *echunk, size_t attr_size);
static inline void bit32_encoded_push(uint32_t v, uint8_t *p);
void chunker_logger(const char *s);


void initChunkPusher() {	
}

void finalizeChunkPusher() {
}

int pushChunkHttp(ExternalChunk *echunk, char *url) {
	void *grapes_chunk_attributes_block=NULL;
	uint8_t *buffer=NULL;
	static size_t attr_size = 0;
	size_t buffer_size = 0;
	double ts1e3 = 0.0;
	double tus3e1 = 0.0;
	int ret = STREAMER_FAIL_RETURN;
	
	attr_size = 5*sizeof(int) + 2*sizeof(time_t) + 2*sizeof(suseconds_t) + 1*sizeof(double);

	/* first pack the chunk info that we get from the streamer into an "attributes" block of a regular GRAPES chunk */
	if(	(grapes_chunk_attributes_block = packExternalChunkToAttributes(echunk, attr_size)) != NULL ) {
		/* then fill-up a proper GRAPES chunk */
		Chunk gchunk;
		gchunk.id = echunk->seq;
		gchunk.size = echunk->payload_len;
		/* convert external_chunk start_time in milliseconds and put it in grapes chunk */
		ts1e3 = (double)(echunk->start_time.tv_sec*1e3);
		tus3e1 = (double)(echunk->start_time.tv_usec/1e3);
		gchunk.timestamp = ts1e3+tus3e1;
		gchunk.attributes = grapes_chunk_attributes_block;
		gchunk.attributes_size = attr_size;
		gchunk.data = echunk->data;

		/* 20 bytes are needed to put the chunk header info on the wire */
		buffer_size = GRAPES_ENCODED_CHUNK_HEADER_SIZE + attr_size + echunk->payload_len;
		if( (buffer = malloc(buffer_size)) != NULL) {
			/* encode the GRAPES chunk into network bytes */
			int32_t encoded_size = encodeChunk(&gchunk, buffer, buffer_size);
			char * str = "chunk";
fprintf(stderr, "Writing chunk of size %d\n", encoded_size);
			write(1,str, 5);
			write(1,&encoded_size, sizeof(encoded_size));
			fprintf(stderr, " written %d\n", write(1,buffer, encoded_size));	//todo check for errors
			free(buffer);
			free(grapes_chunk_attributes_block);
			return ret;
		}
		free(grapes_chunk_attributes_block);
		return ret;
	}
	return ret;
}

