#ifndef _CHUNKER_METADATA_H
#define _CHUNKER_METADATA_H


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "external_chunk.h"

typedef struct chunker_metadata {
    ExternalChunk *echunk;
    int size;
    int strategy;
    int val_strategy; //value of current strategy (number of chunks if first strategy, num of bytes if second strategy)
} ChunkerMetadata;

void chunker_trim(char *s);
ChunkerMetadata *chunkerInit(const char *config);


#endif
