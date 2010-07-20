#ifndef _CHUNKER_METADATA_H
#define _CHUNKER_METADATA_H


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdint.h>
#include <limits.h>
#include <confuse.h>

#include "external_chunk.h"

typedef struct chunker_metadata {
	int strategy;
	//value of current strategy (number of chunks if first strategy, num of bytes if second strategy)
	int val_strategy;
	//chunk IDs are sequence or start times?	
	int cid;
	int base_chunkid_sequence_offset;
	char outside_world_url[1000];
} ChunkerMetadata;


ChunkerMetadata *chunkerInit(void);


#endif
