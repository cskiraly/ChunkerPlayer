/*
 *  Copyright (c) 2009-2011 Carmelo Daniele, Dario Marchese, Diego Reforgiato, Giuseppe Tropea
 *  developed for the Napa-Wine EU project. See www.napa-wine.eu
 *
 *  This is free software; see lgpl-2.1.txt
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <net_helper.h>
#include <trade_msg_ha.h>

#include "external_chunk_transcoding.h"
#include "chunker_streamer.h"

#define MY_IP "192.168.1.67"
#define MY_PORT 4444
#define STREAMER_IP "192.168.1.67"
#define STREAMER_PORT 7051


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
