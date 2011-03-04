/*
 *  Copyright (c) 2009-2011 Carmelo Daniele, Dario Marchese, Diego Reforgiato, Giuseppe Tropea
 *  developed for the Napa-Wine EU project. See www.napa-wine.eu
 *
 *  This is free software; see lgpl-2.1.txt
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_SIZE 512000

static void *receive(void *dummy)
{
  char msg[MAX_SIZE];
  while (1) {
    int32_t msg_size;
    int32_t p = 0;
    read(0, &msg, 5);
    read(0, &msg_size, sizeof(msg_size));
fprintf(stderr, "Reading chunk of size %d\n", msg_size);
    if (msg_size > MAX_SIZE) {
      fprintf(stderr, "chunk too big (%d bytes)\n", msg_size);
      exit(1);
    } else {
      while (p <  msg_size) {
        int r = read(0, &msg + p, msg_size - p);
        if (r >= 0) {
          p += r;
        }
      }
      enqueueBlock(msg, msg_size); //this might take some time
    }
  }
}

void *initChunkPuller() {
  pthread_t stdin_thread;
  pthread_create(&stdin_thread, NULL, receive, NULL); 
  pthread_join(stdin_thread, NULL);

}

void finalizeChunkPuller(void *d) {
}

