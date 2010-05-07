/*
 *  Copyright (c) 2010 Luca Abeni
 *  Copyright (c) 2010 Csaba Kiraly
 *
 *  This is free software; see gpl-3.0.txt
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include <net_helper.h>
#include <chunk.h> 
#include <chunkbuffer.h> 
#include <trade_msg_la.h>
#include <trade_msg_ha.h>
#include <peerset.h>
#include <peer.h>
#include <chunkidset.h>

#include "streaming.h"
#include "output.h"
#include "input.h"
#include "dbg.h"
#include "chunk_signaling.h"
#include "chunklock.h"
#include "topology.h"
#include "measures.h"

#include "scheduler_la.h"

struct chunk_buffer *cb;
static struct input_desc *input;
static int cb_size;
static int transid=0;

int _needs(struct chunkID_set *cset, int cb_size, int cid);

void cb_print()
{
#ifdef DEBUG
  struct chunk *chunks;
  int num_chunks, i, id;
  chunks = cb_get_chunks(cb, &num_chunks);

  dprintf("\tchbuf :");
  i = 0;
  if(num_chunks) {
    id = chunks[0].id;
    dprintf(" %d-> ",id);
    while (i < num_chunks) {
      if (id == chunks[i].id) {
        dprintf("%d",id % 10);
        i++;
      } else if (chunk_islocked(id)) {
        dprintf("*");
      } else {
        dprintf(".");
      }
      id++;
    }
  }
  dprintf("\n");
#endif
}

void stream_init(int size, struct nodeID *myID)
{
  char conf[32];

  cb_size = size;

  sprintf(conf, "size=%d", cb_size);
  cb = cb_init(conf);
  chunkDeliveryInit(myID);
  //init_measures();
}

int source_init(const char *fname, struct nodeID *myID, bool loop)
{
  input = input_open(fname, loop ? INPUT_LOOP : 0);
  if (input == NULL) {
    return -1;
  }

  stream_init(20, myID);
  return 0;
}

struct chunkID_set *cb_to_bmap(struct chunk_buffer *chbuf)
{
  struct chunk *chunks;
  int num_chunks, i;
  struct chunkID_set *my_bmap = chunkID_set_init(0);
  chunks = cb_get_chunks(chbuf, &num_chunks);

  for(i=num_chunks-1; i>=0; i--) {
    chunkID_set_add_chunk(my_bmap, chunks[i].id);
  }
  return my_bmap;
}

// a simple implementation that request everything that we miss ... up to max deliver
struct chunkID_set *get_chunks_to_accept(struct peer *from, const struct chunkID_set *cset_off, int max_deliver){
  struct chunkID_set *cset_acc, *my_bmap;
  int i, d, cset_off_size;
  //double lossrate;

  cset_acc = chunkID_set_init(0);

  //reduce load a little bit if there are losses on the path from this guy
  //lossrate = get_lossrate_receive(from->id);
  //lossrate = finite(lossrate) ? lossrate : 0;	//start agressively, assuming 0 loss
  //if (rand()/((double)RAND_MAX + 1) >= 10 * lossrate ) {
    my_bmap = cb_to_bmap(cb);
    cset_off_size = chunkID_set_size(cset_off);
    for (i = 0, d = 0; i < cset_off_size && d < max_deliver; i++) {
      int chunkid = chunkID_set_get_chunk(cset_off, i);
      //dprintf("\tdo I need c%d ? :",chunkid);
      if (!chunk_islocked(chunkid) && _needs(my_bmap, cb_size, chunkid)) {
        chunkID_set_add_chunk(cset_acc, chunkid);
        chunk_lock(chunkid,from);
        dtprintf("accepting %d from %s", chunkid, node_addr(from->id));
#ifdef MONL
        dprintf(", loss:%f rtt:%f", get_lossrate(from->id), get_rtt(from->id));
#endif
        dprintf("\n");
        d++;
      }
    }
    chunkID_set_free(my_bmap);
  //} else {
  //    dtprintf("accepting -- from %s loss:%f rtt:%f\n", node_addr(from->id), lossrate, get_rtt(from->id));
  //}

  return cset_acc;
}

void send_bmap(struct peer *to)
{
  struct chunkID_set *my_bmap = cb_to_bmap(cb);
   sendMyBufferMap(to->id, my_bmap, cb_size, 0);

  chunkID_set_free(my_bmap);
}

double get_average_lossrate_pset(struct peerset *pset)
{
  int i, n;
  struct peer *neighbours;

  n = peerset_size(pset);
  neighbours = peerset_get_peers(pset);
  {
    struct nodeID *nodeids[n];
    for (i = 0; i<n; i++) nodeids[i] = neighbours[i].id;
#ifdef MONL
    return get_average_lossrate(nodeids, n);
#else
    return 0;
#endif
  }
}

void received_chunk(struct nodeID *from, const uint8_t *buff, int len)
{
  int res;
  static struct chunk c;
  struct peer *p;

  res = decodeChunk(&c, buff + 1, len - 1);
  if (res > 0) {
#ifdef MONL
    reg_chunk_receive(c.id);
#endif
    chunk_unlock(c.id);
    dprintf("Received chunk %d from peer: %s\n", c.id, node_addr(from));
    output_deliver(&c);
    res = cb_add_chunk(cb, &c);
    cb_print();
    if (res < 0) {
      dprintf("\tchunk too old, buffer full with newer chunks\n");
      free(c.data);
      free(c.attributes);
    }
    p = nodeid_to_peer(from,1);
    if (p) {	//now we have it almost sure
      chunkID_set_add_chunk(p->bmap,c.id);	//don't send it back
      send_bmap(p);	//send explicit ack
    }
  } else {
    fprintf(stderr,"\tError: can't decode chunk!\n");
  }
}

int generated_chunk(suseconds_t *delta)
{
  int res;
  struct chunk c;

  *delta = input_get(input, &c);
  if (*delta < 0) {
    fprintf(stderr, "Error in input!\n");
    exit(-1);
  }
  if (c.data == NULL) {
    return 0;
  }
  dprintf("Generated chunk %d of %d bytes\n",c.id, c.size);
  res = cb_add_chunk(cb, &c);
  if (res < 0) {
    free(c.data);
    free(c.attributes);
  }

  return 1;
}

/**
 *example function to filter chunks based on whether a given peer needs them.
 *
 * Looks at buffermap information received about the given peer.
 */
int needs(struct peer *p, struct chunk *c){
  //dprintf("\t%s needs c%d ? :",node_addr(p->id),c->id);
  if (! p->bmap) {
    //dprintf("no bmap\n");
    return 1;	// if we have no bmap information, we assume it needs the chunk (aggressive behaviour!)
  }
  return _needs(p->bmap, p->cb_size, c->id);
}

int _needs(struct chunkID_set *cset, int cb_size, int cid){
  if (chunkID_set_check(cset,cid) < 0) { //it might need the chunk
    int missing, min;
    //@TODO: add some bmap_timestamp based logic

    if (chunkID_set_size(cset) == 0) {
      //dprintf("bmap empty\n");
      return 1;	// if the bmap seems empty, it needs the chunk
    }
    missing = cb_size - chunkID_set_size(cset);
    missing = missing < 0 ? 0 : missing;
    min = chunkID_set_get_chunk(cset, chunkID_set_size(cset)-1);
      //dprintf("%s ... cid(%d) >= min(%d) - missing(%d) ?\n",(cid >= min - missing)?"YES":"NO",cid, min, missing);
    return (cid >= min - missing);
  }

  //dprintf("has it\n");
  return 0;
}

double peerWeightReceivedfrom(struct peer **p){
  return timerisset(&(*p)->bmap_timestamp) ? 1 : 0.1;
}

double peerWeightUniform(struct peer **p){
  return 1;
}

#ifdef MONL
double peerWeightRtt(struct peer **p){
  double rtt = get_rtt((*p)->id);
  //dprintf("RTT to %s: %f\n", node_addr((*p)->id), rtt);
  return finite(rtt) ? 1 / (rtt + 0.005) : 1 / 1;
}
#endif

double getChunkTimestamp(struct chunk **c){
  return (double) (*c)->timestamp;
}

void send_accepted_chunks(struct peer *to, struct chunkID_set *cset_acc, int max_deliver){
  int i, d, cset_acc_size;

  cset_acc_size = chunkID_set_size(cset_acc);
  for (i = 0, d=0; i < cset_acc_size && d < max_deliver; i++) {
    struct chunk *c;
    int chunkid = chunkID_set_get_chunk(cset_acc, i);
    c = cb_get_chunk(cb, chunkid);
    if (c && needs(to, c) ) {	// we should have the chunk, and he should not have it. Although the "accept" should have been an answer to our "offer", we do some verification
      int res = sendChunk(to->id, c);
      if (res >= 0) {
        chunkID_set_add_chunk(to->bmap, c->id); //don't send twice ... assuming that it will actually arrive
        d++;
#ifdef MONL
        reg_chunk_send(c->id);
#endif
      } else {
        fprintf(stderr,"ERROR sending chunk %d\n",c->id);
      }
    }
  }
}

int offer_max_deliver(struct peer *p)
{
#ifdef MONL
  switch (get_hopcount(p->id)) {
    case 0: return 5;
    case 1: return 2;
    default: return 1;
  }
#else
  return 1;
#endif
}

void send_offer()
{
  struct chunk *buff;
  int size, res, i, n;
  struct peer *neighbours;
  struct peerset *pset;

  pset = get_peers();
  n = peerset_size(pset);
  neighbours = peerset_get_peers(pset);
  dprintf("Send Offer: %d neighbours\n", n);
  if (n == 0) return;
  buff = cb_get_chunks(cb, &size);
  if (size == 0) return;

  {
    size_t selectedpeers_len = 1;
    struct chunk *chunkps[size];
    struct peer *peerps[n];
    struct peer *selectedpeers[selectedpeers_len];

    //reduce load a little bit if there are losses on the path from this guy
    double average_lossrate = get_average_lossrate_pset(pset);
    average_lossrate = finite(average_lossrate) ? average_lossrate : 0;	//start agressively, assuming 0 loss
    if (rand()/((double)RAND_MAX + 1) < average_lossrate ) {
      return;
    }

    for (i = 0;i < size; i++) chunkps[i] = buff+i;
    for (i = 0; i<n; i++) peerps[i] = neighbours+i;
    selectPeersForChunks(SCHED_WEIGHTED, peerps, n, chunkps, size, selectedpeers, &selectedpeers_len, needs, peerWeightReceivedfrom);	//select a peer that needs at least one of our chunks

    for (i=0; i<selectedpeers_len ; i++){
      int max_deliver = offer_max_deliver(selectedpeers[i]);
      struct chunkID_set *my_bmap = cb_to_bmap(cb);
      dprintf("\t sending offer(%d) to %s\n", transid, node_addr(selectedpeers[i]->id));
      res = offerChunks(selectedpeers[i]->id, my_bmap, max_deliver, transid++);
      chunkID_set_free(my_bmap);
    }
  }
}


void send_chunk()
{
  struct chunk *buff;
  int size, res, i, n;
  struct peer *neighbours;
  struct peerset *pset;

  pset = get_peers();
  n = peerset_size(pset);
  neighbours = peerset_get_peers(pset);
  dprintf("Send Chunk: %d neighbours\n", n);
  if (n == 0) return;
  buff = cb_get_chunks(cb, &size);
  dprintf("\t %d chunks in buffer...\n", size);
  if (size == 0) return;

  /************ STUPID DUMB SCHEDULING ****************/
  //target = n * (rand() / (RAND_MAX + 1.0)); /*0..n-1*/
  //c = size * (rand() / (RAND_MAX + 1.0)); /*0..size-1*/
  /************ /STUPID DUMB SCHEDULING ****************/

  /************ USE SCHEDULER ****************/
  {
    size_t selectedpairs_len = 1;
    struct chunk *chunkps[size];
    struct peer *peerps[n];
    struct PeerChunk selectedpairs[1];
  
    for (i = 0;i < size; i++) chunkps[i] = buff+i;
    for (i = 0; i<n; i++) peerps[i] = neighbours+i;
    schedSelectPeerFirst(SCHED_WEIGHTED, peerps, n, chunkps, size, selectedpairs, &selectedpairs_len, needs, peerWeightReceivedfrom, getChunkTimestamp);
  /************ /USE SCHEDULER ****************/

    for (i=0; i<selectedpairs_len ; i++){
      struct peer *p = selectedpairs[i].peer;
      struct chunk *c = selectedpairs[i].chunk;
      dprintf("\t sending chunk[%d] to ", c->id);
      dprintf("%s\n", node_addr(p->id));

      send_bmap(p);
      res = sendChunk(p->id, c);
      dprintf("\tResult: %d\n", res);
      if (res>=0) {
        chunkID_set_add_chunk(p->bmap,c->id); //don't send twice ... assuming that it will actually arrive
#ifdef MONL
        reg_chunk_send(c->id);
#endif
      } else {
        fprintf(stderr,"ERROR sending chunk %d\n",c->id);
      }
    }
  }
}
