#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "external_chunk_transcoding.h"

int bit32_encoded_pull(uint8_t *p) {
	int tmp;
  
	memcpy(&tmp, p, CHUNK_TRANSCODING_INT_SIZE);
	tmp = ntohl(tmp);

	return tmp;
}


void bit32_encoded_push(uint32_t v, uint8_t *p) {
	uint32_t tmp;
  
	tmp = htonl(v);
	memcpy(p, &tmp, CHUNK_TRANSCODING_INT_SIZE);
}

static inline void int_cpy(uint8_t *p, int v)
{
  uint32_t tmp;
  
  tmp = htonl(v);
  memcpy(p, &tmp, 4);
}

static inline uint32_t int_rcpy(const uint8_t *p)
{
  uint32_t tmp;
  
  memcpy(&tmp, p, 4);
  tmp = ntohl(tmp);

  return tmp;
}

int encodeChunk(const struct chunk *c, uint8_t *buff, int buff_len)
{
  uint32_t half_ts;

  if (buff_len < 20 + c->size + c->attributes_size) {
    /* Not enough space... */
    return -1;
  }

  int_cpy(buff, c->id);
  half_ts = c->timestamp >> 32;
  int_cpy(buff + 4, half_ts);
  half_ts = c->timestamp;
  int_cpy(buff + 8, half_ts);
  int_cpy(buff + 12, c->size);
  int_cpy(buff + 16, c->attributes_size);
  memcpy(buff + 20, c->data, c->size);
  if (c->attributes_size) {
    memcpy(buff + 20 + c->size, c->attributes, c->attributes_size);
  }

  return 20 + c->size + c->attributes_size;
}

int decodeChunk(struct chunk *c, const uint8_t *buff, int buff_len)
{
  if (buff_len < 20) {
    return -1;
  }
  c->id = int_rcpy(buff);
  c->timestamp = int_rcpy(buff + 4);
  c->timestamp = c->timestamp << 32;
  c->timestamp |= int_rcpy(buff + 8); 
  c->size = int_rcpy(buff + 12);
  c->attributes_size = int_rcpy(buff + 16);

  if (buff_len < c->size + 20) {
    return -2;
  }
  c->data = malloc(c->size);
  if (c->data == NULL) {
    return -3;
  }
  memcpy(c->data, buff + 20, c->size);

  if (c->attributes_size > 0) {
    if (buff_len < c->size + c->attributes_size) {
      return -4;
    }
    c->attributes = malloc(c->attributes_size);
    if (c->attributes == NULL) {
      return -5;
    }
    memcpy(c->attributes, buff + 20 + c->size, c->attributes_size);
  }

  return 20 + c->size + c->attributes_size;
}

void print_block(const uint8_t *b, int size) {
int i=0;
fprintf(stderr,"BEGIN OF %d BYTES---\n", size);
for(i=0; i<size; i++) {
fprintf(stderr,"%d ", *(b+i));
}
fprintf(stderr,"END OF %d BYTES---\n", size);
}


void chunker_logger(const char *s) {
	fprintf(stderr,"%s\n", s);
}


void *packExternalChunkToAttributes(ExternalChunk *echunk, size_t attr_size) {
	void *attr_block = NULL;
	int64_t half_prio;
	int64_t prio = 0.0;
	
	if( (attr_block = malloc(attr_size)) == NULL ) {
		chunker_logger("attrib block malloc failed!");
		return NULL;
	}
	
	/* copy the content of the external_chunk structure into a proper attributes block */
	/* also network-encoding the 4bytes pieces */
	bit32_encoded_push(echunk->seq, attr_block);
	bit32_encoded_push(echunk->frames_num, attr_block + CHUNK_TRANSCODING_INT_SIZE);
	
	/* unfold the timeval structure fields */
	bit32_encoded_push(echunk->start_time.tv_sec, attr_block + CHUNK_TRANSCODING_INT_SIZE*2);
	bit32_encoded_push(echunk->start_time.tv_usec, attr_block + CHUNK_TRANSCODING_INT_SIZE*3);
	bit32_encoded_push(echunk->end_time.tv_sec, attr_block + CHUNK_TRANSCODING_INT_SIZE*4);
	bit32_encoded_push(echunk->end_time.tv_usec, attr_block + CHUNK_TRANSCODING_INT_SIZE*5);
	
	bit32_encoded_push(echunk->payload_len, attr_block + CHUNK_TRANSCODING_INT_SIZE*6);
	bit32_encoded_push(echunk->len, attr_block + CHUNK_TRANSCODING_INT_SIZE*7);
	bit32_encoded_push(echunk->category, attr_block + CHUNK_TRANSCODING_INT_SIZE*8);
	/* this is a double, should be 64bits, split it */
	prio = (uint64_t)echunk->priority;
	half_prio = prio >> 32;
	bit32_encoded_push(half_prio, attr_block + CHUNK_TRANSCODING_INT_SIZE*9);
	half_prio = prio;
	bit32_encoded_push(half_prio, attr_block + CHUNK_TRANSCODING_INT_SIZE*10);
	/* ref count is not needed over the wire */
	
	return attr_block;
}


ExternalChunk *grapesChunkToExternalChunk(Chunk *gchunk) {
	uint64_t tmp_prio;
	ExternalChunk *echunk = (ExternalChunk *)malloc(sizeof(ExternalChunk));
	if(!echunk) {
		fprintf(stderr,"Memory error in chunkToExternalchunk!\n");
		return NULL;
	}
	/* pull out info from the attributes block from the grapes chunk */
	echunk->seq = bit32_encoded_pull(gchunk->attributes);
	echunk->frames_num = bit32_encoded_pull(gchunk->attributes + CHUNK_TRANSCODING_INT_SIZE);
	echunk->start_time.tv_sec = bit32_encoded_pull(gchunk->attributes + CHUNK_TRANSCODING_INT_SIZE*2);
	echunk->start_time.tv_usec = bit32_encoded_pull(gchunk->attributes + CHUNK_TRANSCODING_INT_SIZE*3);
	echunk->end_time.tv_sec = bit32_encoded_pull(gchunk->attributes + CHUNK_TRANSCODING_INT_SIZE*4);
	echunk->end_time.tv_usec = bit32_encoded_pull(gchunk->attributes + CHUNK_TRANSCODING_INT_SIZE*5);
	echunk->payload_len = bit32_encoded_pull(gchunk->attributes + CHUNK_TRANSCODING_INT_SIZE*6);
	echunk->len = bit32_encoded_pull(gchunk->attributes + CHUNK_TRANSCODING_INT_SIZE*7);
	echunk->category = bit32_encoded_pull(gchunk->attributes + CHUNK_TRANSCODING_INT_SIZE*8);
	tmp_prio = bit32_encoded_pull(gchunk->attributes + CHUNK_TRANSCODING_INT_SIZE*9);
	tmp_prio = tmp_prio << 32;
	tmp_prio |= bit32_encoded_pull(gchunk->attributes + CHUNK_TRANSCODING_INT_SIZE*10);
	echunk->priority = (double)tmp_prio;

	/* pass the payload along */
	echunk->data = gchunk->data;

	return echunk;
}
