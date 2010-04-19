#include <curl/curl.h>

#include "external_chunk_transcoding.h"
#include "chunker_streamer.h"


void initChunkPusher();
void finalizeChunkPusher();
int sendViaCurl(Chunk gchunk, size_t attr_size, ExternalChunk *echunk, char *url);


void initChunkPusher() {	
	/* In windows, this will init the winsock stuff */ 
	curl_global_init(CURL_GLOBAL_ALL);
}

void finalizeChunkPusher() {
	curl_global_cleanup();
}


int sendViaCurl(Chunk gchunk, size_t attr_size, ExternalChunk *echunk, char *url) {
	//MAKE THE CURL EASY HANDLE GLOBAL? TO REUSE IT?
	CURL *curl_handle;
	struct curl_slist *headers=NULL;
	uint8_t *buffer=NULL;
	size_t buffer_size = 0;
	int ret = STREAMER_FAIL_RETURN;

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
			curl_easy_perform(curl_handle); /* post away! */
			curl_slist_free_all(headers); /* free the header list */
			ret = STREAMER_OK_RETURN;
		}
		/* always cleanup curl */ 
		curl_easy_cleanup(curl_handle);
		free(buffer);
	}
	return ret;
}
