/*
 *  Copyright (c) 2009-2011 Carmelo Daniele, Dario Marchese, Diego Reforgiato, Giuseppe Tropea
 *  developed for the Napa-Wine EU project. See www.napa-wine.eu
 *
 *  This is free software; see lgpl-2.1.txt
 */

#define CURL_STATICLIB
#include <curl/curl.h>

#include "streamer_commons.h"

void initChunkPusher();
void finalizeChunkPusher();
int sendViaCurl(Chunk gchunk, int buffer_size, char *url);

//MAKE THE CURL EASY HANDLE GLOBAL TO REUSE IT CONNECTIONS
CURL *curl_handle = 0;

void initChunkPusher() {
	/* In windows, this will init the winsock stuff */ 
	curl_global_init(CURL_GLOBAL_ALL);
	/* get a curl handle */ 
	curl_handle = curl_easy_init();
	fprintf(stderr, "CURL client initialized with handle %p\n", curl_handle);
}

void finalizeChunkPusher() {
	/* always cleanup curl */ 
	curl_easy_cleanup(curl_handle);
	fprintf(stderr, "CURL client finalized handle %p\n", curl_handle);
	curl_global_cleanup();
}

int sendViaCurl(Chunk gchunk, int buffer_size, char *url) {
	struct curl_slist *headers=NULL;
	uint8_t *buffer=NULL;

	int ret = STREAMER_FAIL_RETURN;

	if( (buffer = malloc(buffer_size)) != NULL) {
		/* encode the GRAPES chunk into network bytes */
		encodeChunk(&gchunk, buffer, buffer_size);

		if(curl_handle) {
			curl_easy_setopt(curl_handle, CURLOPT_URL, url);
			/* fill the headers */
			headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
			/* disable Expect: header */
			headers = curl_slist_append(headers, "Expect:");
			/* enable Connection: keep-alive */
			//headers = curl_slist_append(headers, "Connection: keep-alive");
			/* enable chunked */
			//headers = curl_slist_append(headers, "Transfer-Encoding: chunked");
			/* force HTTP 1.0 */
			//curl_easy_setopt (curl_handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);
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

		free(buffer);
	}
	return ret;
}
