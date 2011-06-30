#ifndef _EXTERNAL_CHUNK_TRANSCODING_H
#define _EXTERNAL_CHUNK_TRANSCODING_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <chunk.h>

#include "external_chunk.h"

#define CHUNK_TRANSCODING_INT_SIZE 4
//this should be in chunk.h and used in som's chunk_encoding.c
#define GRAPES_ENCODED_CHUNK_HEADER_SIZE 20

/**
 * commodity function to dump a block of bytes
 */
void print_block(const uint8_t *b, int size);

/**
 * transform a grapes chunk into an external chunk
 * provided the grapes chunk has the appropriate attributes section
 */
ExternalChunk *grapesChunkToExternalChunk(Chunk *gchunk);

/**
 * pack the extra information held into the external chunk structure
 * into a proper attributes section of a grapes chunk
 */
void *packExternalChunkToAttributes(ExternalChunk *echunk, size_t attr_size);

/**
 * theese are copied from GRAPES
 */
int encodeChunk(const struct chunk *c, uint8_t *buff, int buff_len);
int decodeChunk(struct chunk *c, const uint8_t *buff, int buff_len);
int bit32_encoded_pull(uint8_t *p);
void bit32_encoded_push(uint32_t v, uint8_t *p);

#endif
