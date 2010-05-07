	/*
 *  Copyright (c) 2010 Csaba Kiraly
 *
 *  This is free software; see gpl-3.0.txt
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include <chunk.h>
#include <net_helper.h>

#include "http_default_urls.h"
#include "external_chunk_transcoding.h"

#include "output.h"
#include "dbg.h"

#define PLAYER_IP "127.0.0.1"

struct nodeID *streamer;

void output_init(int bufsize)
{
  //streamer = create_node(PLAYER_IP, PLAYER_PORT);
}

void output_deliver(const struct chunk *c)
{
  int ret = -1;
  char url[256];

	sprintf(url, "http://%s:%d%s", PLAYER_IP, UL_DEFAULT_EXTERNALPLAYER_PORT, UL_DEFAULT_EXTERNALPLAYER_PATH);
  ret = sendViaCurl(*c, GRAPES_ENCODED_CHUNK_HEADER_SIZE + c->size + c->attributes_size, url);
  dprintf("Chunk %d delivered to %s\n", c->id, url);
}
