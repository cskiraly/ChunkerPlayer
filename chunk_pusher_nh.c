#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <net_helper.h>
#include <trade_msg_ha.h>

#include "external_chunk_transcoding.h"
#include "chunker_streamer.h"

#define MY_IP "127.0.0.1"
#define MY_PORT 4444
#define STREAMER_IP "127.0.0.1"
#define STREAMER_PORT 8888

struct nodeID *streamer;

int initChunkPusher() {
	struct nodeID *myID = net_helper_init(MY_IP, MY_PORT);
	if(! myID) {
	    fprintf(stderr,"Error initializing net_helper: port %d used by something else?", MY_PORT);
	    return -1;
	}
	chunkDeliveryInit(myID);
	streamer = create_node(STREAMER_IP, STREAMER_PORT);

	return 1;
}

void finalizeChunkPusher() {
}

write_chunk(struct chunk *c)
{
	sendChunk(streamer, c);
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

		write_chunk(&gchunk);

		free(grapes_chunk_attributes_block);
		return ret;
	}
	return ret;
}

