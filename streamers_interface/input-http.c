/*
 *  Copyright (c) 2010 Csaba Kiraly
 *
 *  This is free software; see gpl-3.0.txt
 */
#include <platform.h>
#include <microhttpd.h>

#include <chunk.h>
#include <http_default_urls.h>
#include "external_chunk_transcoding.h"

#include "input.h"

extern struct chunk_buffer *cb;
pthread_mutex_t cb_mutex;

struct input_desc {
  int dummy;
};

struct MHD_Daemon *httpd;

struct input_desc *input_open(const char *fname, uint16_t flags)
{
  struct input_desc *res;

  res = malloc(sizeof(struct input_desc));
  if (res == NULL) {
    return NULL;
  }

  res->dummy = 0;
dprintf("BEFORE INIT! %d\n", res->dummy);
  pthread_mutex_init(&cb_mutex, NULL);
  //this daemon will listen the network for incoming chunks from a streaming source
  //on the following path and port
  httpd = initChunkPuller(UL_DEFAULT_CHUNKBUFFER_PATH, UL_DEFAULT_CHUNKBUFFER_PORT);
dprintf("AFTER INIT! %d\n", res->dummy);

  return res;
}

void input_close(struct input_desc *s)
{
  free(s);
  finalizeChunkPuller(httpd);
}

//this one is not used, just routinely called by the firging thread
int input_get(struct input_desc *s, struct chunk *c)
{
  c->data = NULL;
  return 0;
}

//this is the real one, called by the http receiver thread
int enqueueBlock(const uint8_t *block, const int block_size) {
	static int ExternalChunk_header_size = 5*CHUNK_TRANSCODING_INT_SIZE + 2*CHUNK_TRANSCODING_INT_SIZE + 2*CHUNK_TRANSCODING_INT_SIZE + 1*CHUNK_TRANSCODING_INT_SIZE*2;
  int decoded_size = 0;
	int res = -1;
//  struct chunk gchunk;

  Chunk* gchunk=NULL;
	gchunk = (Chunk *)malloc(sizeof(Chunk));
	if(!gchunk) {
		printf("Memory error in gchunk!\n");
		return -1;
	}

  decoded_size = decodeChunk(gchunk, block, block_size);

  if(decoded_size < 0 || decoded_size != GRAPES_ENCODED_CHUNK_HEADER_SIZE + ExternalChunk_header_size + gchunk->size) {
	    fprintf(stderr, "chunk %d probably corrupted!\n", gchunk->id);
		return -1;
	}

  if(cb) {
  	pthread_mutex_lock(&cb_mutex);
  	res = cb_add_chunk(cb, gchunk);
  	pthread_mutex_unlock(&cb_mutex);
  }
  if (res < 0) { //chunk sequence is older than previous chunk (SHOULD SEND ANYWAY!!!)
    free(gchunk->data);
    free(gchunk->attributes);
    free(gchunk);
    fprintf(stderr, "Chunk %d of %d bytes FAIL res %d\n", gchunk->id, gchunk->size, res);
  }
  else {
    pthread_mutex_lock(&cb_mutex);
    send_chunk(); //push it
    pthread_mutex_unlock(&cb_mutex);
    dprintf("Chunk %d of %d bytes PUSHED res %d\n", gchunk->id, gchunk->size, res);
  }

  return 0;
}

