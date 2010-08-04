#include <curl/curl.h>

#include "streamer_commons.h"

void initChunkPusher();
void finalizeChunkPusher();
int sendViaCurl(Chunk gchunk, int buffer_size, char *url);


void initChunkPusher() {	
	/* In windows, this will init the winsock stuff */ 
	curl_global_init(CURL_GLOBAL_ALL);
}

void finalizeChunkPusher() {
	curl_global_cleanup();
}

int sendViaCurl(Chunk gchunk, int buffer_size, char *url) {
	//MAKE THE CURL EASY HANDLE GLOBAL? TO REUSE IT?
	CURL *curl_handle;
	struct curl_slist *headers=NULL;
	uint8_t *buffer=NULL;

	int ret = STREAMER_FAIL_RETURN;

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
