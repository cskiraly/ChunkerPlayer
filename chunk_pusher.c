#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "external_chunk_transcoding.h"
#include "chunker_streamer.h"

#define DEBUG_PUSHER

void initChunkPusher();
void finalizeChunkPusher();
int pushChunkHttp(ExternalChunk *echunk, char *url);
void *chunkToAttributes(ExternalChunk *echunk, size_t attr_size);
static inline void bit32_encoded_push(uint32_t v, uint8_t *p);
void chunker_logger(const char *s);

extern ChunkerMetadata *cmeta;


void initChunkPusher() {	
	/* In windows, this will init the winsock stuff */ 
	curl_global_init(CURL_GLOBAL_ALL);
}

void finalizeChunkPusher() {
	curl_global_cleanup();
}

int pushChunkHttp(ExternalChunk *echunk, char *url) {
	//MAKE THE CURL EASY HANDLE GLOBAL? TO REUSE IT?
	CURL *curl_handle;
	struct curl_slist *headers=NULL;
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
		gchunk.size = echunk->payload_len;
		/* convert external_chunk start_time in milliseconds and put it in grapes chunk */
		ts1e3 = (double)(echunk->start_time.tv_sec*1e3);
		tus3e1 = (double)(echunk->start_time.tv_usec/1e3);
		gchunk.timestamp = ts1e3+tus3e1;

		//decide how to create the chunk ID
		if(cmeta->cid == 0) {
			gchunk.id = echunk->seq;
#ifdef DEBUG_PUSHER
			fprintf(stderr, "CHUNKER: packaged SEQ chunkID %d\n", gchunk.id);
#endif
		}
		else if(cmeta->cid == 1) {
			gchunk.id = ts1e3+tus3e1; //its ID is its start time
#ifdef DEBUG_PUSHER
			fprintf(stderr, "CHUNKER: packaged TS chunkID %d\n", gchunk.id);
#endif
		}
		gchunk.attributes = grapes_chunk_attributes_block;
		gchunk.attributes_size = attr_size;
		gchunk.data = echunk->data;

		/* 20 bytes are needed to put the chunk header info on the wire */
		buffer_size = GRAPES_ENCODED_CHUNK_HEADER_SIZE + attr_size + echunk->payload_len;
		if( (buffer = malloc(buffer_size)) != NULL) {
			/* encode the GRAPES chunk into network bytes */
			encodeChunk(&gchunk, buffer, buffer_size);
			/* get a curl handle */ 
			curl_handle = curl_easy_init();
			if(curl_handle) {
				curl_easy_setopt(curl_handle, CURLOPT_URL, url);
				/* fill the headers */
				headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
				/* disable Expect: header */
				headers = curl_slist_append(headers, "Expect:");
				/* post binary data */
				curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, buffer);
				/* set the size of the postfields data */
				curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, buffer_size);
				/* pass our list of custom made headers */
				curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
//print_block(buffer, buffer_size);
				curl_easy_perform(curl_handle); /* post away! */
				curl_slist_free_all(headers); /* free the header list */
				ret = STREAMER_OK_RETURN;
			}
			/* always cleanup curl */ 
			curl_easy_cleanup(curl_handle);
			free(buffer);
			free(grapes_chunk_attributes_block);
			return ret;
		}
		free(grapes_chunk_attributes_block);
		return ret;
	}
	return ret;
}

