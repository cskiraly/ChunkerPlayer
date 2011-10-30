/*
 *  Copyright (c) 2009-2011 Carmelo Daniele, Dario Marchese, Diego Reforgiato, Giuseppe Tropea
 *  Copyright (c) 2010-2011 Csaba Kiraly
 *  developed for the Napa-Wine EU project. See www.napa-wine.eu
 *
 *  This is free software; see lgpl-2.1.txt
 */

#ifndef CHUNK_PUSHER_H
#define CHUNK_PUSHER_H

struct output;

struct output *initTCPPush(char* ip, int port);
void finalizeTCPChunkPusher(struct output *o);
int pushChunkTcp(struct output *o, ExternalChunk *echunk);

#endif
